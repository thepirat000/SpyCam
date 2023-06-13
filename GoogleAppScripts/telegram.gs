const telegram_chat_id = 0;
const telegram_token = "1234:xxxxx";

function notifyImageTelegram(device, filename, imageUrl, deviceStatus) {
  // message to send, supporting markdown
  const caption = `${device}: [${filename}](${imageUrl})\n\n${deviceStatus}`; 

  const telegramSendUrl = `https://api.telegram.org/bot${telegram_token}/sendPhoto?chat_id=${telegram_chat_id}&parse_mode=Markdown&caption=${encodeURIComponent(caption)}&photo=${encodeURIComponent(imageUrl)}`;
  
  const response = UrlFetchApp.fetch(telegramSendUrl);
  console.log("Telegram response: " + response.getResponseCode());
}

function sendMessageTelegram(message) {
  const telegramSendUrl = `https://api.telegram.org/bot${telegram_token}/sendMessage?chat_id=${telegram_chat_id}&parse_mode=Markdown&text=${encodeURIComponent(message)}`;
  
  const response = UrlFetchApp.fetch(telegramSendUrl);
  console.log("Telegram response: " + response.getResponseCode());
}

function __test() {
  notifyImageTelegram("CAM_TEST_1", "filename.jpg", "https://drive.google.com/uc?export=view&id=1XMkHUJ2eqzMjCwD40evtmLk8M8Zsglju", "hola 123");
  //sendMessageTelegram("FEDE Test [LINK](http://fede.com)\ntest");
}