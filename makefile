install: mytest.c
	gcc -o ./mqtt-telemetry mqtt-telemetry.cpp -lbcm2835 - lssl -lcrypto -lpaho-mqtt3cs -ljwt