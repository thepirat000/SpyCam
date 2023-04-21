const telegram_chat_id = 999999999;
const telegram_token = "XXXXXXXXX";

function notifyImageTelegram(device, filename, imageUrl) {
  const caption = `${device}: [${filename}](${imageUrl})`; // "message to send supporting markdown [ok](http://test.com)";
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
  //notifyImageTelegram("Test", "Capture 123456.jpg", "https://drive.google.com/uc?export=view&id=1EECXpCFvajg6tc7K5TmMine0tdm8bq4l");
  sendMessageTelegram("FEDE", "Test [LINK](http://fede.com) test");
}