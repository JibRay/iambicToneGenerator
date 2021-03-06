/*
  iambicToneGenerator.ino

  Input is a two-paddle key. output is an iambic tone to headphones.

  Processor is Arduino Nano, connected as follows (see defines below):

    TOME_PIN is tone output. It is connected to phone jack through a 750 ohm
    resistor and a 15 uF capacitor to ground.

    DIT_PIN is dit paddle input. Paddle grounds this pin when active.

    DAH_PIN is dah paddle input. Paddle grounds this pin when active.
  
  Rules:
  -Briefly pressing the dit paddle produces one dit tone.
  -Briefly pressing the dah paddle produces one dah tone.
  -Holding either paddle produces a continuous string of dits or dahs until the
   paddle is released.
  -Holding both paddles produces a string of dit-dah tones. The leading tone
   is dit if the dit paddle is pressed first or dah if the dah paddle is first.
   Releasing the dit paddle continues with a string of dahs, or dits if only
   the dah paddle is released.
  -All dits and dahs are followed by a period of silence equal to one dit
   time.

  Change TONE_FREQ to change the tone frequency.
  
  To change the timing, change the CODE_SPEED value below. All element timing
  is based on this value which is based on the 'PARIS' convention. The word
  PARIS contains 50 dit element equivalents. The silence between dits and
  dahs is one dit time. The dah time is three times as long as a dit.

  The program is two state machines. The main loop is a state machine that
  evaluates the paddle inputs. This passes the desired tone pattern to the
  tone state machine.
 */

//===========================================================================
#define VERSION 2

// These defines set the behavior of the program:
#define CODE_SPEED   20                     // Words per minute.
#define TONE_FREQ    800                    // Hertz.
#define BAUDRATE     19200

//===========================================================================
// Calculated dit and dah times. These are according to the 'PARIS' timing
// specs. The word PARIS contains 50 timing units.
#define WORD_TIME    60.0 / CODE_SPEED      // In seconds.
#define DIT_TIME     1000 * WORD_TIME / 50  // In msecs.
#define DAH_TIME     DIT_TIME * 3           // In msecs.

// Pin definitions:
#define LED_PIN      13  // Used only for debug.
#define TONE_PIN     12
#define DIT_PIN       2
#define DAH_PIN       3

// States:
#define IDLE_STATE    0
#define DIT_INPUT     1
#define DAH_INPUT     2
#define DIT_DAH_INPUT 3
#define DAH_DIT_INPUT 4
#define TONE_ON       1
#define TONE_PAUSE    2

#define QUIET         0
#define DIT           1
#define DAH           2
#define DIT_DAH       3
#define DAH_DIT       4

// Command states.
#define CMD_IDLE      0
#define CMD_ARGUMENT  1

// Argument states.
#define PRINT_HELP    0
#define SET_SPEED     1

#define LINE_BUFFER_SIZE 80

//============================================================================
// Types

struct Command {
  int state;
  int command;
  int index;
  char line[80];
  void begin(int cmd) {
    index = 0;
    for (int i = 0; i < LINE_BUFFER_SIZE; ++i)
      line[i] = '\0';
    state = CMD_ARGUMENT;
    command = cmd;
  }
};

//===========================================================================
// Global variables:
int toneFrequency;
int state, toneState, lastTone, toneType;
unsigned long toneTimer, wordTime, ditTime, dahTime;
Command command;

//===========================================================================
// Global functions.

// speed argument is words per minute.
void setWordSpeed(int speed) {
  unsigned long wordTime = (unsigned long)(60.0 / (float)speed);
  ditTime = (unsigned long)(1000.0 * (float)wordTime / 50.0);
  dahTime = (unsigned long)((float)ditTime * 3.0);
}

void updateCommands() {
  int codeSpeed;
  
  while (Serial.available() > 0) {
    char c = Serial.read();
    switch(command.state) {
      case CMD_IDLE:
        switch(c) {
          case '?':
            command.begin(PRINT_HELP);
            break;
          case 'w':
            Serial.print(F("Set code speed to "));
            Serial.println(F(" words per minute."));
            break;
          default:
            Serial.print(F("Unrecognized command "));
            Serial.println(c);
        }
        break;
      case CMD_ARGUMENT:
        switch(command.command) {
          case PRINT_HELP:
            if (c == '\r' || c == '\n') {
              printHelp();
              command.state = CMD_IDLE;
            }
            break;
          case SET_SPEED:
            if (c == '\r' || c == '\n') {
              codeSpeed = atoi(command.line);
              setWordSpeed(codeSpeed);
              Serial.println();
              command.state = CMD_IDLE;
            } else if (c >= '0' && c <= '9') {
              command.line[command.index++] = c;
              Serial.print(c);
              if (command.index >= LINE_BUFFER_SIZE)
                command.state = CMD_IDLE;
            }
            break;
        }
        break;
    }
  }
}

void printHelp() {
  Serial.println();
  Serial.print(F("Iambic Tone Generator version "));
  Serial.println(VERSION);
  Serial.println(F("Commands:"));
  Serial.println(F("?          Print this help."));
  Serial.println(F("w speed    Set speed as words per minute."));
}

void updateMainState() {
  bool ditKeyInput = digitalRead(DIT_PIN) == LOW;
  bool dahKeyInput = digitalRead(DAH_PIN) == LOW;
  
  switch(state) {
    case IDLE_STATE:
      if (ditKeyInput) {
        toneType = DIT;
        state = DIT_INPUT;
      } else if (dahKeyInput) {
        toneType = DAH;
        state = DAH_INPUT;
      } else {
        toneType = QUIET;
      }
      break;
    case DIT_INPUT:
      // If neither paddle is closed stop the tone. If the dah paddle is
      // now closed change the state to send repeating di-dah. Otherwise
      // continue to send dits.
      if (!ditKeyInput && !dahKeyInput) {
        toneType = QUIET;
        state = IDLE_STATE;
      } else if (ditKeyInput && dahKeyInput) {
        toneType = DIT_DAH;
        state = DIT_DAH_INPUT;
      }
      break;
    case DAH_INPUT:
      // If neither paddle is closed stop the tone. If the dit paddle is
      // now closed change state to send repeating dah-dit. Otherwise
      // continue to send dahs.
      if (!ditKeyInput && !dahKeyInput) {
        toneType = QUIET;
        state = IDLE_STATE;
      } else if (ditKeyInput && dahKeyInput) {
        toneType = DAH_DIT;
        state = DIT_DAH_INPUT;
      }
      break;
    case DIT_DAH_INPUT:
      // If either paddle is open switch back to sending dits or dahs by
      // by going to the idle state.
      if (!ditKeyInput || !dahKeyInput) {
        toneType = QUIET;
        state = IDLE_STATE;
      }
      break;
  }
}

// Update the state of the tone output. Depending on the toneState, send
// repeating dits, dahs or di-dahs. Each dit or dah is always followed by a
// quiet period (pause) equal to one dit time. Return true if at the end of
// a complete dit or dah tone including the following pause.
bool updateToneState(int toneType) {
  bool done = false;
  
  switch (toneState) {
    case IDLE_STATE:
      switch (toneType) {
        case DIT:
        case DIT_DAH:
          toneTimer = millis() + DIT_TIME;
          tone(TONE_PIN, TONE_FREQ);
          lastTone = DIT;
          toneState = TONE_ON;
          break;
        case DAH:
        case DAH_DIT:
          toneTimer = millis() + DAH_TIME;
          tone(TONE_PIN, TONE_FREQ);
          lastTone = DAH;
          toneState = TONE_ON;
          break;
      }
      break;
    case TONE_ON:
      if (millis() > toneTimer) {
        noTone(TONE_PIN);
        toneTimer = millis() + DIT_TIME;
        toneState = TONE_PAUSE;
      }
      break;
    case TONE_PAUSE:
      if (millis() > toneTimer) {
        switch (toneType) {
          case QUIET:
          case DIT:
          case DAH:
            toneState = IDLE_STATE;
            lastTone = QUIET;
            done = true;
            break;
          case DIT_DAH:
          case DAH_DIT:
            if (DIT == lastTone) {
              toneTimer = millis() + DAH_TIME;
              tone(TONE_PIN, TONE_FREQ);
              lastTone = DAH;
              toneState = TONE_ON;
            } else {
              toneTimer = millis() + DIT_TIME;
              tone(TONE_PIN, TONE_FREQ);
              lastTone = DIT;
              toneState = TONE_ON;
            }
            break;
        }
      }
      break;
  }
  return done;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(TONE_PIN, OUTPUT);
  pinMode(DIT_PIN, INPUT_PULLUP);
  pinMode(DAH_PIN, INPUT_PULLUP);

  Serial.begin(BAUDRATE);

  setWordSpeed(CODE_SPEED);
  toneFrequency = TONE_FREQ;
  state = toneState = IDLE_STATE;
  lastTone = QUIET;

  command.state = CMD_IDLE;

  printHelp();
}

//===========================================================================
// The main loop:
void loop() {
  updateCommands();
  updateMainState();
  updateToneState(toneType);
}
