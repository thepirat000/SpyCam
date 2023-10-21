#pragma once

extern bool isStreaming;

void startCameraServer(const char *user, const char *password, int serverPort);
void stopCameraServer();
