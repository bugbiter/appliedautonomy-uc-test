// [START iot_mqtt_include]
// Paho MQTT client
#define _XOPEN_SOURCE 500
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "jwt.h"
#include "openssl/ec.h"
#include "openssl/evp.h"
#include "MQTTClient.h"
// [END iot_mqtt_include]

// [START bcm2835_include]
#include "bcm2835.h"
// [END bcm2835_include]

#define TRACE 1 /* Set to 1 to enable tracing */
#define PIN RPI_BPLUS_GPIO_J8_40 //GPIO21

struct {
  char* address;
  enum { clientid_maxlen = 256, clientid_size };
  char clientid[clientid_size];
  char* deviceid;
  char* keypath;
  char* projectid;
  char* region;
  char* registryid;
  char* rootpath;
  enum { topic_maxlen = 256, topic_size };
  char topic[topic_size];
  char* payload;
  char* algorithm;
} opts = {
  .address = "ssl://mqtt.googleapis.com:8883",
  .clientid = "projects/appliedautonomybackend/locations/europe-west1/registries/rest-registry/devices/Iot_rpi3",
  .deviceid = "Iot_rpi3",
  .keypath = "ec_private.pem",
  .projectid = "appliedautonomybackend",
  .region = "europe-west1",
  .registryid = "rest-registry",
  .rootpath = "roots.pem",
  .topic = "/devices/Iot_rpi3/events",
  .payload = "Hello world!",
  .algorithm = "ES256"
};

volatile MQTTClient_deliveryToken deliveredtoken;

void delivered(void *context, MQTTClient_deliveryToken dt)
{
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    int i;
    char* payloadptr;
    printf("Message arrived\n");
    printf("     topic: %s\n", topicName);
    printf("   message: ");
    payloadptr = message->payload;
    for(i=0; i<message->payloadlen; i++)
    {
        putchar(*payloadptr++);
    }
    putchar('\n');
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

void connlost(void *context, char *cause)
{
    printf("\nConnection lost\n");
    printf("     cause: %s\n", cause);
}

/**
 * Calculates issued at / expiration times for JWT and places the time, as a
 * Unix timestamp, in the strings passed to the function. The time_size
 * parameter specifies the length of the string allocated for both iat and exp.
 */
static void GetIatExp(char* iat, char* exp, int time_size) {
  // TODO: Use time.google.com for iat
  time_t now_seconds = time(NULL);
  snprintf(iat, time_size, "%lu", now_seconds);
  snprintf(exp, time_size, "%lu", now_seconds + 3600);
  if (TRACE) {
    printf("IAT: %s\n", iat);
    printf("EXP: %s\n", exp);
  }
}

static int GetAlgorithmFromString(const char *algorithm) {
    if (strcmp(algorithm, "RS256") == 0) {
        return JWT_ALG_RS256;
    }
    if (strcmp(algorithm, "ES256") == 0) {
        return JWT_ALG_ES256;
    }
    return -1;
}

/**
 * Calculates a JSON Web Token (JWT) given the path to a EC private key and
 * Google Cloud project ID. Returns the JWT as a string that the caller must
 * free.
 */
static char* CreateJwt(const char* ec_private_path, const char* project_id, const char *algorithm) {
  char iat_time[sizeof(time_t) * 3 + 2];
  char exp_time[sizeof(time_t) * 3 + 2];
  uint8_t* key = NULL; // Stores the Base64 encoded certificate
  size_t key_len = 0;
  jwt_t *jwt = NULL;
  int ret = 0;
  char *out = NULL;

  // Read private key from file
  FILE *fp = fopen(ec_private_path, "r");
  if (fp == (void*) NULL) {
    printf("Could not open file: %s\n", ec_private_path);
    return "";
  }
  fseek(fp, 0L, SEEK_END);
  key_len = ftell(fp);
  fseek(fp, 0L, SEEK_SET);
  key = malloc(sizeof(uint8_t) * (key_len + 1)); // certificate length + \0

  fread(key, 1, key_len, fp);
  key[key_len] = '\0';
  fclose(fp);

  // Get JWT parts
  GetIatExp(iat_time, exp_time, sizeof(iat_time));

  jwt_new(&jwt);

  // Write JWT
  ret = jwt_add_grant(jwt, "iat", iat_time);
  if (ret) {
    printf("Error setting issue timestamp: %d\n", ret);
  }
  ret = jwt_add_grant(jwt, "exp", exp_time);
  if (ret) {
    printf("Error setting expiration: %d\n", ret);
  }
  ret = jwt_add_grant(jwt, "aud", project_id);
  if (ret) {
    printf("Error adding audience: %d\n", ret);
  }
  ret = jwt_set_alg(jwt, GetAlgorithmFromString(algorithm), key, key_len);
  if (ret) {
    printf("Error during set alg: %d\n", ret);
  }
  out = jwt_encode_str(jwt);
  if(!out) {
      perror("Error during token creation:");
  }
  // Print JWT
  if (TRACE) {
    printf("JWT: [%s]\n", out);
  }

  jwt_free(jwt);
  free(key);
  return out;
}

static const int kQos = 1;
static const unsigned long kTimeout = 10000L;
static const char* kUsername = "ignored";

static const unsigned long kInitialConnectIntervalMillis = 500L;
static const unsigned long kMaxConnectIntervalMillis = 6000L;
static const unsigned long kMaxConnectRetryTimeElapsedMillis = 900000L;
static const float kIntervalMultiplier = 1.5f;

int Publish(char* payload, int payload_size) {
  int rc = -1;
  MQTTClient client = {0};
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token = {0};

  MQTTClient_create(&client, opts.address, opts.clientid, MQTTCLIENT_PERSISTENCE_NONE, NULL);
  conn_opts.keepAliveInterval = 60;
  conn_opts.cleansession = 1;
  conn_opts.username = kUsername;
  conn_opts.password = CreateJwt(opts.keypath, opts.projectid, opts.algorithm);
  MQTTClient_SSLOptions sslopts = MQTTClient_SSLOptions_initializer;
  sslopts.trustStore = opts.rootpath;
  sslopts.privateKey = opts.keypath;
  conn_opts.ssl = &sslopts;
  //MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

  unsigned long retry_interval_ms = kInitialConnectIntervalMillis;
  unsigned long total_retry_time_ms = 0;
  /*if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to connect, return code %d\n", rc);
    exit(EXIT_FAILURE);
  }*/
  while ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
    if (rc == 3) {  // connection refused: server unavailable
      usleep(retry_interval_ms / 1000);
      total_retry_time_ms += retry_interval_ms;
      if (total_retry_time_ms >= kMaxConnectRetryTimeElapsedMillis) {
        printf("Failed to connect, maximum retry time exceeded.");
        exit(EXIT_FAILURE);
      }
      retry_interval_ms *= kIntervalMultiplier;
      if (retry_interval_ms > kMaxConnectIntervalMillis) {
        retry_interval_ms = kMaxConnectIntervalMillis;
      }
    } else {
      printf("Failed to connect, return code %d\n", rc);
      exit(EXIT_FAILURE);
    }
  }

  pubmsg.payload = payload;
  pubmsg.payloadlen = payload_size;
  pubmsg.qos = kQos;
  pubmsg.retained = 0;
  MQTTClient_publishMessage(client, opts.topic, &pubmsg, &token);
  printf("Waiting for up to %lu seconds for publication of %s\n"
          "on topic %s for client with ClientID: %s\n",
          (kTimeout/1000), opts.payload, opts.topic, opts.clientid);
  //while(deliveredtoken != token);
  rc = MQTTClient_waitForCompletion(client, token, kTimeout);
  printf("Message with delivery token %d delivered\n", token);
  MQTTClient_disconnect(client, 10000);
  MQTTClient_destroy(&client);

  return rc;
}

int main(int argc, char* argv[])
{
  if (!bcm2835_init())
    return 1;
  
  // set RPI pin to be an input
  bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_INPT);

  OpenSSL_add_all_algorithms();
  OpenSSL_add_all_digests();
  OpenSSL_add_all_ciphers();

  while (1)
  {
    // Read some data
    uint8_t value = bcm2835_gpio_lev(PIN);
    printf("GPIO21: %d\n", value);
    opts.payload = value;
    int result = Publish(opts.payload, strlen(opts.payload));
    // wait a bit
    delay(1000);
  }

  bcm2835_close();
  EVP_cleanup();
}