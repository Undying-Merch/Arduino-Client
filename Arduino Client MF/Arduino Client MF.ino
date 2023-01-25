#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <TimeLib.h>
#include "DHT.h"
#include <ArduinoJson.h>

//Variable for the timer
int timerCount = 0;
//Tempoary String - Used for warning msg
String tempoString = "";

//Variables to 'change'.
//The opening hours
int openingHour = 8;
//The closing Hours
int closingHour = 18;
//Timezone (GMT)
int gmt = 1;
//Time for when it should send to DB (in minutes)
int timerMinutes = 10;
//If true Temperature will be uploaded in Celcius, if false, the will be uploaded in Fahrenheit
bool celcius = true;
//Min temp before it sends a warning (in Celcius)
int minTemp = 18;
//Max temp before it send a warning (in Celcius)
int maxTemp = 21;
//Max humidity before it sends a warning.
int maxHum = 50;
//min humidity before it sends a warning.
int minHum = 30;

//Variables for getting time
//Boards Mac Address
byte mac[] = {
  0xA8, 0x61, 0x0A, 0xAE, 0x94, 0xD3
};
unsigned int localPort = 8888;       // Local port to listen for UDP packets
const char timeServer[] = "time.google.com"; // time.google.com NTP server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //Buffer to hold incoming and outgoing packets
EthernetUDP Udp;

//Settings for HTTP
int    HTTP_PORT   = 8000;
String HTTP_METHOD = "POST";
char   HOST_NAME[] = "192.168.1.114";
String PATH_NAME   = "/data/opretmaalinger/";
// Connect to the HTTP server
EthernetClient client;

//Variables for Sound
//Their placement on the board
const int pinAdc = A0;
const int buttonPin = 5;
int alarmLed = 3;
//Set to check when the button is pressed
int buttonState = 0;
//Variables used throughout the code
long soundSum = 0;
long highestRecordedSound = 0;
String soundString = "";
String soundWarningString = "Sound exceded safe rates";
int maxSoundValue = 500;
bool soundWarningBool = false;
bool soundAlarm = false;
bool alarmSounded = false;
bool lightOn = false;


//Variables for DHT
//Values recorded from the DHT
int tempInt = 0;
int humInt = 0;
//The values converted to String
String tempString = "";
String humString = "";
//Defining which DHT sensor we're using.
#define DHTTYPE DHT11
#define DHTPIN 2     // what pin we're connected to
DHT dht(DHTPIN, DHTTYPE);   //   DHT11

//Automated check of which board we're using, and thus creating a Serial called Debug.
#if defined(ARDUINO_ARCH_AVR)
    #define debug  Serial

#elif defined(ARDUINO_ARCH_SAMD) ||  defined(ARDUINO_ARCH_SAM)
    #define debug  SerialUSB
#else
    #define debug  Serial
#endif

void setup() {
  //Create the PinModes for our LED and BTN.
  pinMode(alarmLed, OUTPUT);
  pinMode(buttonPin, INPUT);
  
  debug.begin(9600);
  while (!Serial) {
    ; // Wait for serial port to connect. Needed for native USB port only
  }

  // Start Ethernet and UDP
  if (Ethernet.begin(mac) == 0) {
    debug.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      debug.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      debug.println("Ethernet cable is not connected.");                      
    }
  }
  //Start the UDP
  Udp.begin(localPort);
  //Start DHT
  dht.begin();
}

void loop() {
  //Checks if the system has time, if not downloads from the internet.
  if(hour() == NULL){
    getTime();
    setTimer();
  }
  //Gets sound from soundRecorder.
  getSound();  

  //Checks if we're inside opening hours.
  if(hour() >= openingHour && hour() < closingHour){
    if(minute() == timerCount){
      getDHT();
      setWarning();
      sendData();      
    }
  }
  //Checks if we're outside the opening hours.
  else if(hour() < openingHour || hour() >= closingHour){
    if(highestRecordedSound >= maxSoundValue){
      soundAlarm = true;
      getDHT();     
      theAlarm();
    }
    if(timerCount != 0){
      timerCount = 0;
    }
  }
  //Keeps the Ethernet running.  
  Ethernet.maintain();
}


// send an NTP request to the time server at the given address
void sendNTPpacket(const char * address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//Sets the timer for the upload schedule.
void setTimer(){
  //Sets the timer, based on what it was defined as in the top.    
  timerCount = minute() + timerMinutes;
  if(timerCount >= 60){
    timerCount = timerCount - 60;
  }
  debug.print("Next data-collection is at: ");
  debug.println(timerCount);
  debug.println("");
}

//Gets time and puts it into the Time functions.
void getTime(){

  sendNTPpacket(timeServer); // send an NTP packet to a time server

  // wait to see if a reply is available
  delay(1000);

  if (Udp.parsePacket()) {
    // We've received a packet, read the data from it
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    // the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;

    int theHour = (epoch  % 86400L) / 3600 + gmt; // print the hour (86400 equals secs per day)
    int theMinute = (epoch  % 3600) / 60; // print the minute (3600 equals secs per minute)
    int theSecond = epoch % 60; // print the second

    setTime(theHour, theMinute, theSecond, 1, 2, 00);
  }
  delay(1000);
}

//Gets the DHT data.
void getDHT(){
    float temp_hum_val[2] = {0};
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)

    //Checks if we're getting data from the DHT.
    if (!dht.readTempAndHumidity(temp_hum_val)) {
        //Puts the data into variables.
        humInt = temp_hum_val[0];
        tempInt = temp_hum_val[1];        
        
        //Puts the data into Strings
        humString = String(humInt);
        humString = humString + " %";
        if(celcius == true){
          tempString = String(tempInt);
          tempString = tempString + " *C";
        }
        //If Celcius has been defined as false, will make the temp string into Fahrenheit.        
        else{
          int tempTemp = tempInt * 1.8 + 32;
          tempString = String(tempTemp);
          tempString = tempString + " *F";
        }
    } else {
        debug.println("Failed to get temprature and humidity value.");
    }

    delay(1000);
}

//Creates the warning String.
void setWarning(){  
  //Checks if the Alarm has sounded.
  if(alarmSounded == true){
    tempoString = tempoString + "An alarm has sounded at the location ";
  }
  //Checks if the temperature is to low or high  
  if(tempInt < minTemp){
    if(tempoString != ""){
      tempoString = tempoString + "& ";      
    }
    tempoString = tempoString + "The temperature is too cold ";    
  }
  else if(tempInt > maxTemp){
    if(tempoString != ""){
      tempoString = tempoString + "& ";      
    }
    tempoString = tempoString + "The temperature is too warm ";    
  }
  //Checks if the humidity is to low or high.
  if(humInt < minHum){
    if(tempoString != ""){
      tempoString = tempoString + "& ";      
    }
    tempoString = tempoString + "The humidity is to low ";     
  }
  else if(humInt > maxHum){
    if(tempoString != ""){
      tempoString = tempoString + "& ";      
    }
    tempoString = tempoString + "The humidity is to high ";   
  }
  //Checks if the sound level has been to high, unless it's an Alarm going of. 
    if(soundWarningBool == true && alarmSounded == false){
    if(tempoString != ""){
      tempoString = tempoString + "& ";      
    }
    tempoString = tempoString + "Sound exceded safe rates ";
  }
 
  
}

//Checks the sound level from the Sound recorder
void getSound(){
  
    for(int i=0; i<32; i++)
    {
        soundSum += analogRead(pinAdc);
    }
 
    soundSum >>= 5;

    if(soundSum > maxSoundValue){
      soundWarningBool = true;
    }
    if(soundSum > highestRecordedSound){
      highestRecordedSound = soundSum;      
    }    
    soundString = String(highestRecordedSound);
    delay(100);
}

//Sends the data to the database.
void sendData(){
  //Creates the JSON document, while putting the info needed into it.
  StaticJsonDocument<200> doc;
  doc["tempratur"] = tempInt;
  doc["luftfugtighed"] = humInt;
  doc["advarsel"] = tempoString;
  doc["alarm"] = alarmSounded;   
  
  //A bool to test if we've connected to the database.        
  bool test;

  //Connect to the host via speciffied port
  client.connect(HOST_NAME, HTTP_PORT);
  //Tell the server what we're gonna do
  client.println(HTTP_METHOD + " " + PATH_NAME + " HTTP/1.1");
  // Send the HTTP headers
  client.println("Host: 192.168.1.114");
  client.println("Authorization: Token d2b6345c35b8d0a8b7116e5109076832012e760b");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(measureJson(doc));
  client.println("Content-Type: application/json");
  // Terminate headers with a blank line
  client.println();
  // Send JSON document in body
  test = serializeJson(doc, client);

  //Prints all our data which should be sent to the Database to our console, so we can keep check on it here to.
  debug.print("Temp: ");
  debug.println(tempString);
  debug.print("Hum: ");
  debug.println(humString);
  debug.print("Warning: ");
  debug.println(tempoString);
  debug.print("Sound level: ");
  debug.println(soundString);

  //Used to test if we had a connection to the database.
  if(test == true){
    Serial.println("Database contacted.");
  }
  else{
    Serial.println("Something went wrong trying to send data.");
  }

  //Resets all the variables who might have been used.
  //If the alarm has rung.
  alarmSounded = false;
  //If sound levels have exceeded set-limit.
  soundWarningBool = false;
  //Resets the highest recorded sound volume
  highestRecordedSound = 0;
  //The string used for warning msg.
  tempoString = "";


  setTimer();
}

//The alarm that will sound if sound levels are to high outside opening hours.
void theAlarm(){
  //Tells the rest of the program the alarm has sounded.  
  alarmSounded = true;
  //A loop to keep turning a light on and off and a "Wee"/"Woo" text to emulate a sounding alarm, until a button is pressed    
  while(soundAlarm == true){
    buttonState = digitalRead(buttonPin);
    if(lightOn == false){
      digitalWrite(alarmLed, HIGH);
      lightOn = true;
      debug.println("Wee");
    }
    else{
      digitalWrite(alarmLed, LOW);
      lightOn = false;
      debug.println("Woo");
    }
    if(buttonState == HIGH){
      soundAlarm = false;
      digitalWrite(alarmLed, LOW);    
    }
    delay(500);
    
  }
  //Resets the highest recorded sound
  highestRecordedSound = 0;

  //Create a warning
  setWarning();
  //Send the data to the Database.
  sendData();

}




