#pragma once

// WiFi access point + web server (own FreeRTOS task). The radio is off until webStart() is called,
// because WiFi current peaks can disturb the magnetometer.
void webBegin();  // registers the routes and creates the task (call once, after settingsLoad())
void webStart();  // starts the access point "GS-MC-XXXX" and the web server
void webStop();   // switches the access point off again
