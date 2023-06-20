/* ThePirat 2023 - Telegram helper */
#pragma once

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

typedef void (*HandleMessage)(const String& text, const String& chat_id, const String& from);

bool isMoreDataAvailable();
byte *getNextBuffer();
int getNextBufferLen();

class Telegram {
public:
  Telegram(const String& token, const String& defaultChatId, HandleMessage handleMessageCallback);
  void ProcessInputMessages();
  void SendMessage(const String& text);
  void SendImage(uint8_t *buffer, unsigned long len);
  void SendMessageWithReplyKeyboard(const String& text, const String& jsonKeyboard);
  bool SetCommands(const String& commands);

private:
  UniversalTelegramBot *bot;
  WiFiClientSecure *client;
  HandleMessage handleMessageCallback;
  String _token;
  String _defaultChatId;
  String _lastChatId;
  
  void handleNewMessages(int numNewMessages);
  String getChatId();
};
