#include <ESP8266WiFi.h>
#include <MQ135.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "dht.h"
#include "Base64.h"
#define ANALOGPIN A0
#define dht_apin D5 // Analog Pin sensor is connected to
#include "AES.h"

AES aes;
dht DHT;

const char *ssid = "SSID";
const char *password = "password";

const char *mqttServer = "IP Address of cloud server";
const int mqttPort = 1883;
const char *mqttUser = "user";
const char *mqttPassword = "password";

String devId = "5b59a30a0f7e613d2b4548f7";  
String custId = "5b574235ceebec3db3cd130c";

String topic = "aqms/" + devId + "/aq";
String topic1 = "aqms/" + devId + "/hd";

char b64data[1024];
byte iv[N_BLOCK];

//byte key[] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };
byte key[] = {0x6A, 0x31, 0x71, 0x77, 0x6E, 0x35, 0x79, 0x76, 0x38, 0x65, 0x6C, 0x64, 0x61, 0x73, 0x73, 0x61};

// The unitialized Initialization vector
byte my_iv[N_BLOCK] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

uint8_t getrnd()
{
    uint8_t really_random = *(volatile uint8_t *)0x3FF20E44;
    return really_random;
}

// Generate a random initialization vector
void gen_iv(byte *iv)
{
    for (int i = 0; i < N_BLOCK; i++)
    {
        iv[i] = (byte)getrnd();
    }
}

WiFiClient espClient;
PubSubClient client(espClient);

MQ135 gasSensor = MQ135(ANALOGPIN);

void setup()
{
    // put your setup code here, to run once:
    Serial.begin(9600);
    delay(100);
    pinMode(2, OUTPUT);
    Serial.println();
    WiFi.begin(ssid, password);
    int i = 0;
    while ((i <= 10) && (WiFi.status() != WL_CONNECTED))
    {
        delay(500);
        Serial.print(".");
        i++;
    }
    Serial.println();
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    client.setServer(mqttServer, mqttPort);
    client.setCallback(callback);

    while (!client.connected())
    {
        Serial.println("Connecting to MQTT...");

        if (client.connect(devId.c_str()))
        {
            Serial.println("connected");
        }
        else
        {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }
    client.subscribe("sendData");
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);

    Serial.print("Message:");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();
    Serial.println("-----------------------");
}

void loop()
{
    //  Serial.println(" IV b64: " + String(b64data));
    DHT.read11(dht_apin);
    // put your main code here, to run repeatedly:
    float rzero = gasSensor.getRZero(); //this to get the rzero value, uncomment this to get ppm value
    // float ppm = gasSensor.getPPM();     // this to get ppm value, uncomment this to get rzero value
    float ppm = gasSensor.getCorrectedPPM(DHT.temperature, DHT.humidity);

    DynamicJsonBuffer jsonBuffer;
    JsonObject &accRec = jsonBuffer.createObject();

    accRec["aq"] = ppm;
    accRec["t"] = DHT.temperature;
    accRec["h"] = DHT.humidity;
    accRec["dId"] = devId;
    accRec["cId"] = custId;
    String AccData = "";
    accRec.printTo(AccData);

    char * EncryptData = encryptString(AccData);
    Serial.println ("Encrypted data in base64: " + String(EncryptData) );

    sendDataoverMqtt(EncryptData,topic);

    JsonObject &healthData = jsonBuffer.createObject();
    healthData["devId"] = devId;
    healthData["custId"] = custId;
    healthData["rss"] = WiFi.RSSI();
    healthData["heap"] = ESP.getFreeHeap();
    healthData["vcc"] = ESP.getVcc();
    String HealthData = "";
    healthData.printTo(HealthData);

    char * EncryptData1 = encryptString(HealthData);
    Serial.println ("Encrypted data in base64 2: " + String(EncryptData1) );

    sendDataoverMqtt(EncryptData1,topic1);

    digitalWrite(2, HIGH);
    delay(500);
    digitalWrite(2, LOW);
    delay(500);
    delay(4000);
}

void sendDataoverMqtt(String toSend,String tp)
{
    DynamicJsonBuffer jsonBuffer;
    JsonObject &sndData = jsonBuffer.createObject();
    sndData["devId"] = devId;
    sndData["pk"] = String(b64data);
    sndData["d"] = String(toSend);
    sndData["t"] = millis();
    String d;
    sndData.printTo(d);
    Serial.println(d);
    Serial.println("publishing topic ");
    client.publish(tp.c_str(), d.c_str());
}

char *encryptString(String StrToEncrypt)
{
    //Encryption data declaration
    char b64data1[1024];
    char decoded[1024];
    byte cipher[1024];

    gen_iv(my_iv); // Generate a random IV
    // Print the IV
    base64_encode(b64data, (char *)my_iv, N_BLOCK);

    int b64len = base64_encode(b64data1, (char *)StrToEncrypt.c_str(), StrToEncrypt.length());
    Serial.println(" Message in B64: " + String(b64data1));
    Serial.println(" The lenght is:  " + String(b64len));

    // For sanity check purpose
    base64_decode(decoded, b64data1, b64len);
    //  Serial.println("Decoded: " + String(decoded));

    // Encrypt! With AES128, our key and IV, CBC and pkcs7 padding
    aes.do_aes_encrypt((byte *)b64data1, b64len, cipher, key, 128, my_iv);

    Serial.println("Encryption done!");

    base64_encode(b64data1, (char *)cipher, aes.get_size());

    return b64data1;
}

