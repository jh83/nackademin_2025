export const handler = async (event) => {
  console.log("Received Event: ", event);

  //const webhookUrl = process.env.WebhookURL;
  const webhookUrl = "https://discord.com/api/webhooks/15682729632/no6n4mn2Z1cVI";

  const discordMessage = {
    "content": event.eventType,
    "embeds": [
        {
            "title": "Temperature: " + event.temperature + "°C",
            "description": `at <t:${Math.floor(event.timestamp / 1000)}:F>`,
            "color": "45973"
        }
    ]
  };

  const r = await fetch(webhookUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(discordMessage),
  });

  if (r.status !== 204) {
    const text = await r.text();
    console.error("Discord webhook error:", r.status, text);
    throw new Error(`Discord ${r.status}`);
  }

  console.log("Discord webhook response:", r.status);

  return;
};