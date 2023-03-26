function sendEmail(message) {
  var emailQuotaRemaining = MailApp.getRemainingDailyQuota();
  Logger.log("Remaining email quota: " + emailQuotaRemaining);

  MailApp.sendEmail({
    to: "thepirat000@hotmail.com",
    subject: "Test ESP32",
    htmlBody: message,
    inlineImages: { }
  });

}
