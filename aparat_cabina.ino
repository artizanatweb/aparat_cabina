#include "SIM900.h"
#include <SoftwareSerial.h>
//We don't need the http functions. So we can disable the next line.
//#include "inetGSM.h"
#include "sms.h"
#include "call.h"

// BUTTON pin
#define buttonPin 2

// BUZZER pin
#define buzzer 11

// RED Led
#define REDLED A5
// RED Led
#define BLUELED A4
// RED Led
#define GREENLED A3


//**************************************************************************
char number[]="0723846180";  //Destination number 
char text[]="OK";  //SMS to send
byte type_sms=SMS_UNREAD;      //Type of SMS
byte del_sms=1;                //0: No deleting sms - 1: Deleting SMS

// SERVER number -- SIM position
const int ServerPosition=1;
// SIM last position
int SIMPositions=2;

// pozitia actuala a indexului SIM
const int firstSimPosition=2;
int actualSimPosition=2;

// Pe SIM este obligatoriu sa existe 2 numere de telefon!
// numerele pot fi identice!

const int numberLength = 10;

char* replyMessage[] = {"false","true"};
//**************************************************************************

CallGSM call;
SMSGSM sms;
char sms_rx[160]; //Received text SMS
char sms_length = 160;
//int inByte=0;    //Number of byte received on serial port
char number_incoming[20];
char dummy_number[20];
char emergency_number[20];
char server_number[20];
byte stat=0;
int error;


// ultima apasate
volatile long lastPushTime = 0;

// Dupa ce interval devine butonul acesibil
int pushLag = 2000;

volatile int buttonStatus = 1; // enabled

// 0 => RED, 2 => BLUE, 1 => GREEN
int leds[3] = {REDLED, BLUELED, GREENLED};

// call started
int callStarted = 0;
// time when the call started
long callStartedAt = 0;
// time to call before hang up and call another number from the phonebook
int callingTime = 30000;

// default call status check
int defaultCallStatusCheck = 10000;
// usable call status check
int callStatusCheck = defaultCallStatusCheck;
// emergency call status check
int emergencyCallStatusCheck = 500;
// last call status check
long lastCallStatusCheck = 0;

// sms status check
int smsStatusCheck = 20000;
// last sms status check
long lastSmsStatusCheck = 0;


//**************************************************************************

// TASKS TO DO IN LOOP

// send SIM action status by SMS
int simActionStatus = 0; // daca mai mare de 0 se trimite simActionResp
int simActionResp = 0; // daca mai mare de 0 se trimite numarul dupa care se reseteaza
long startSimActStatus;

// remove remaining numbers on SIM
int clearSimStatus = 0;
int simDelPos = 0;
long startClearSim;

//**************************************************************************

char fillMe[10];

void setup() {
  Serial.begin(9600);
  //**** INIT DIY Shield ************
  int i = 0;
  while (leds[i]) {
    pinMode(leds[i], OUTPUT);
    i++;
  }
  
  // light RED pin
  closeLED();
  lightLED(0);
  
  pinMode(buzzer, OUTPUT);
  //**** END init DIY Shield ********
  
  
  //**** START GSM MODULE INITIALIZATION ******
  if (gsm.begin(9600))
    Serial.println("\nstatus=READY");
  else Serial.println("\nstatus=IDLE");
  //**** END GSM MODULE INITIALIZATION ********
  
  
  // only at the end of setup
  pinMode(buttonPin, INPUT);
  // start listening for button presses
  attachInterrupt(0, buttonPushed, FALLING);
  
  // se considera ca modulul este on
  closeLED();
  timeLED(1, 2000);
  beep(50);
  
  Serial.println(firstSimPosition);
  
}

void loop() {
  if (buttonStatus == 0) {
    if (digitalRead(buttonPin) == LOW) {
      //
      if (millis() - lastPushTime >= pushLag) {
        buttonStatus = 2;
        Serial.println(actualSimPosition);
        beep(50);
        beep(50);
        beep(50);
        
        closeLED();
        lightLED(0);
        
        // start calling
        actualSimPosition = firstSimPosition;
        
        Serial.println(actualSimPosition);
        
        InitEmergencyCall();
      }
    } else {
      buttonStatus = 1;
      callStarted = 0;
    }
  }
  
  if (millis() - lastCallStatusCheck > callStatusCheck) {
    Check_Call(); //Check if there is an incoming call
  }
  
  if (millis() - lastSmsStatusCheck > smsStatusCheck) {
    Check_SMS();  //Check if there is SMS 
  }
  
  // check if you need to report sim actions
  if (simActionStatus==1) {
    if (millis() - startSimActStatus > 5000) {
      // send a report SMS
      if (simActionResp == 0) {
        sms.SendSMS(ServerPosition,"false");
      } else {
        // conversie int to char* asa cum se face
        itoa(simActionResp,fillMe,10);
        sms.SendSMS(ServerPosition,fillMe);
      }
      
      simActionStatus = 0;
      simActionResp = 0;
    }
  }
  
  // check if you need to empty the rest of the sim positions
  if (clearSimStatus == 1) {
    if (millis() - startClearSim > 7000) {
      Serial.println("***********************");
      Serial.println("Sterg pozitie sim");
      Serial.println(simDelPos);
      Serial.println("***********************");
      emptySimPositions();
    }
  }
}


void InitEmergencyCall()
{
  callStarted = 1;
  callStartedAt = millis();
  callStatusCheck = emergencyCallStatusCheck;
  
  if (call.CallStatus()!=CALL_NONE)         
   {
     Serial.println("Hang");
     call.HangUp();  
     // wait 2 seconds
     long now = millis();
     while (millis() - now < 2000) {
       ;
     }     
   }
   
   Serial.println(actualSimPosition);
   
   call.Call(actualSimPosition);
   
   error=gsm.GetPhoneNumber(actualSimPosition,emergency_number);
   Serial.println(emergency_number);
   if (error!=0)
   {
     if (actualSimPosition > SIMPositions) {
       SIMPositions = actualSimPosition;
     }
     Serial.print("Calling ");
     Serial.println(emergency_number);
     call.Call(actualSimPosition);
   }
   else 
   {
     Serial.print("No number in pos ");
     Serial.println(actualSimPosition);
     
     actualSimPosition = firstSimPosition;
     InitEmergencyCall();
   }
   
   return;
}


void Check_SMS()  //Check if there is an sms 'type_sms'
{
   lastSmsStatusCheck = millis();
   char pos_sms_rx;  //Received SMS position     
   pos_sms_rx=sms.IsSMSPresent(type_sms);
   if (pos_sms_rx!=0)
   {
     //Read text/number/position of sms
     sms.GetSMS(pos_sms_rx,number_incoming,sms_rx,sms_length);
     Serial.print("Received SMS from ");
     Serial.print(number_incoming);
     Serial.print("(sim position: ");
     Serial.print(word(pos_sms_rx));
     Serial.println(")");
     Serial.println(sms_rx);
     if (del_sms==1)  //If 'del_sms' is 1, i delete sms 
     {
       error=sms.DeleteSMS(pos_sms_rx);
       if (error==1)Serial.println("SMS deleted");      
       else Serial.println("SMS not deleted");
     }
     
     error=gsm.GetPhoneNumber(ServerPosition,server_number);
     if (error!=0) {
       Serial.println("Server number found");
       
       // copy incoming number to a new char but starting with second character
       // +0723846180 => 0723846180
       char stripNumber[20];
       int y=0;
       for (int i=2; i < strlen(number_incoming); i++) {
       //for (int i=2; i < 16; i++) {
          stripNumber[y] = number_incoming[i];
          y++;
       }
       int lastIndex = strlen(number_incoming) - 2;
       stripNumber[lastIndex]='\0';/* null character manually added */
       // END number copy
       
       Serial.println("***********************");
       Serial.println(stripNumber);
       if (0 == strcmp(stripNumber, server_number)) {
         Serial.println("Numbers are IDENTICAL !");
         processMessage(sms_rx);
       } else {
         Serial.println("numbers don't match");
       }
       Serial.println("***********************");
     } else {
       Serial.println("***********************");
       Serial.println("Error:");
       Serial.println(error);
       Serial.println("***********************");
     }
     
   } else {
     Serial.println("No SMS");
   }
   return;
   // if (0 == strcmp(phone_number, sim_phone_number)) {
}

void Check_Call()  //Check status call if this is available
{
   lastCallStatusCheck = millis();
   stat=call.CallStatusWithAuth(number_incoming,ServerPosition,SIMPositions);
   switch (stat)
   {    
   case CALL_NONE:
     Serial.println("no call");
     if (callStarted == 1) {
       // nu a raspuns
       closeLED();
       lightLED(0);
       actualSimPosition++;
       // suna inca o data 
       InitEmergencyCall();
     } else {
       callStatusCheck = defaultCallStatusCheck;
       buttonStatus = 1;
       closeLED();
     }
     break;
     case CALL_INCOM_VOICE_AUTH:   
       Serial.print("incoming voice call from ");     
       Serial.println(number_incoming);
       call.PickUp();
       break;
     case CALL_INCOM_VOICE_NOT_AUTH:
       Serial.println("incoming voice call not auth");
       call.HangUp();
       break;
     case CALL_INCOM_DATA_AUTH:
       Serial.println("data auth");
       call.PickUp();
       break;
     case CALL_INCOM_DATA_NOT_AUTH:
       Serial.println("data not auth");
       call.HangUp();
       break;
     case CALL_ACTIVE_DATA:
       Serial.println("call active data");
       break;
     case CALL_ACTIVE_VOICE:
       Serial.println("active voice call");    
       break;
     case CALL_COMM_LINE_BUSY:
       Serial.println("linie ocupata");
       break;
     case CALL_TALKING:
       Serial.println("A raspuns!!!");
       closeLED();
       lightLED(1);
       callStarted = 0;
       actualSimPosition = firstSimPosition;
       break;
     case CALL_ALERTING:
       Serial.println("Suna...");
       if (millis() - callStartedAt > callingTime) {
         // nu a raspuns
         closeLED();
         lightLED(0);
         actualSimPosition++;
         // suna inca o data 
         InitEmergencyCall();
       }
       break;
     case CALL_DIALING:
       Serial.println("Formeaza numarul...");
       closeLED();
       lightLED(2);
       break;
     case CALL_NO_RESPONSE:
       Serial.println("no response");
       break;
   }
   return;
}

void buttonPushed()
{
  if (buttonStatus == 1) {
    buttonStatus = 0;
    lastPushTime = millis();
  }
}

void beep(unsigned char delayms)
{
  long actualTime = millis();
  analogWrite(buzzer, 20);
  while (millis() - actualTime < delayms) {
    ;
  }
  analogWrite(buzzer, 0);
  
  while (millis() - actualTime < delayms+delayms) {
    ;
  }
}

void lightLED(int ledIndex)
{
  if (leds[ledIndex]) {
    digitalWrite(leds[ledIndex], HIGH);
  }
}

void closeLED()
{
  int i = 0;
  while (leds[i]) {
    digitalWrite(leds[i], LOW);
    i++;
  }
}

void timeLED(int ledIndex, int timeMs)
{
  if (leds[ledIndex]) {
    digitalWrite(leds[ledIndex], HIGH);
    
    long actualTime = millis();
    
    while (millis() - actualTime < timeMs) {
      ;
    }
    digitalWrite(leds[ledIndex], LOW);
  }
}

void processMessage(char *smsMessage) {
  char writeToSim[] = "wrs";
  char machieStatusCheck[] = "msc";
  int smsLength = strlen(smsMessage);
  if (smsLength > 2) {
    // explode first 3 letters
    // to obtain the command
    char command[10];
    strncpy(command,smsMessage,3);
    command[3]='\0';/* null character manually added */
    
    Serial.println("***********************");
      Serial.println(command);
      Serial.println("***********************");
    
    if (0 == strcmp(command, writeToSim)) {
      Serial.println("***********************");
      Serial.println("Write numbers to sim");
      Serial.println("***********************");
      if (smsLength > 13) {
        //
        int pos;
        char numberToStore[20];
        
        int y=0;
        int posIndex=3;
        int startPositions=3;
        int writeLoopCount = 1;
        int serverWrite=0;
        int numbersWritten=0;
        
        for (int i=startPositions; i<strlen(smsMessage);i++) {
          if (i == posIndex) {
            char val = smsMessage[i];
            //pos = atoi(val);
            pos = val - '0';
            posIndex = i+numberLength+1;
            y=0;
          } else {
            numberToStore[y] = smsMessage[i];
            y++;
            
            if (y == numberLength) {
              numberToStore[numberLength]='\0';/* null character manually added */
              // write number to SIM
              Serial.println("***********************");
              Serial.println(pos);
              Serial.println(numberToStore);
              if (pos==1 && writeLoopCount==1) {
                // schimba numarul serverului
                error=gsm.WritePhoneNumber(ServerPosition,number);
                Serial.println(error);
                serverWrite++;
              } else if (pos>1 && serverWrite==0) {
                // scrie numarul pe SIM
                error=gsm.WritePhoneNumber(pos,numberToStore);
                Serial.println(error);
                if (error!=0) {
                  numbersWritten++;
                }
              }
              Serial.println("***********************");
              writeLoopCount++;
            }
          }
        }
        
        // daca a ajuns aici
        // inseamna ca modificarea numerelor a reusit
        // acum sterge numere de pe SIM in caz ca exista pozitii mai mari
        if (pos>=2 && serverWrite==0) {
          simActionStatus = 1;
          simActionResp = numbersWritten;
          startSimActStatus=millis();
          
          clearSimStatus = 1;
          simDelPos = pos;
          startClearSim = millis();
        }
      }
    } else if (0 == strcmp(command, machieStatusCheck)) {
      //
      sms.SendSMS(ServerPosition,text);
    }
  }
  return;
}

void emptySimPositions()
{
  simDelPos++;
  error = gsm.GetPhoneNumber(simDelPos,dummy_number);
  if (error!=0) {
    gsm.DelPhoneNumber(simDelPos);
    startClearSim = millis();
  } else {
    clearSimStatus = 0;
    simDelPos = 0;
  }
}

