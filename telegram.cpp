/* ThePirat 2023 - Telegram helper using UniversalTelegramBot library v1.3.0*/
#include "telegram.h"

unsigned long buffer_pos = 0;
unsigned long buffer_length = 0;
const unsigned long CHUNK_SIZE = 8000UL;
uint8_t *imageBuffer; 
unsigned long imageLen;

Telegram::Telegram(const String& token, const String& defaultChatId, long message_id, HandleMessage handleMessageCallback) 
{
    this->handleMessageCallback = handleMessageCallback;
    _token = token;
    _defaultChatId = defaultChatId;
    this->client = new WiFiClientSecure();
    this->client->setInsecure();
    this->bot = new UniversalTelegramBot(token, *this->client);
    this->bot->last_message_received = message_id;
}

void Telegram::ProcessInputMessages() 
{
    Serial.println("Will process telegram messages. Offset: " + String(bot->last_message_received));

    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);

    while (numNewMessages)
    {
        handleNewMessages(numNewMessages);
        numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    }

    Serial.println("Telegram messages processing completed.");
}

void Telegram::SendMessage(const String& text) 
{
    Serial.println("Sending message to telegram: " + text);
    bot->sendMessage(getChatId(), text, "markdown");
}

void Telegram::SendMessageWithReplyKeyboard(const String& text, const String& jsonKeyboard) 
{
  Serial.println("Sending keyboard message to telegram: " + text);
  bot->sendMessageWithReplyKeyboard(getChatId(), text, "markdown", jsonKeyboard, true);
}

void Telegram::SendImage(uint8_t *buffer, unsigned long len)
{
  imageBuffer = buffer;
  imageLen = len;
  buffer_pos = 0;

  Serial.println("Sending image to telegram, Len: " + String(imageLen));

  bot->sendPhotoByBinary(getChatId(), "image/jpeg", imageLen, isMoreDataAvailable, nullptr, getNextBuffer, getNextBufferLen);
}

bool Telegram::SetCommands(const String& commands) 
{
  return bot->setMyCommands(commands);
}

void Telegram::handleNewMessages(int numNewMessages)
{
  for (int i = 0; i < numNewMessages; i++)
  {
    String chat_id = bot->messages[i].chat_id;
    String text = bot->messages[i].text;
    String from_name = bot->messages[i].from_name;
    
    Serial.println("got message: '" + text + "' from: " + from_name + " chat id: " + String(chat_id));
    
    _lastChatId = chat_id;

    this->handleMessageCallback(text, chat_id, from_name, bot->last_message_received);
  }
}

String Telegram::getChatId()
{
  return _lastChatId.length() > 0 ? _lastChatId : _defaultChatId;
}

/* Send image callbacks */
bool isMoreDataAvailable()
{
  bool moreData = imageBuffer && buffer_pos < imageLen;
  return moreData;
}

byte *getNextBuffer()
{
  if (imageBuffer && buffer_pos < imageLen)
  {
    byte* nextBuffer = &imageBuffer[buffer_pos];
    buffer_length = min(CHUNK_SIZE, imageLen - buffer_pos);
    buffer_pos += buffer_length;

    return nextBuffer;
  }
  else
  {
    return nullptr;
  }
}

int getNextBufferLen()
{
  if (imageBuffer)
  {
    return buffer_length;
  }
  else
  {
    return 0;
  }
}
