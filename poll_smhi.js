import { DynamoDBClient } from "@aws-sdk/client-dynamodb";
import {
  DynamoDBDocumentClient,
  PutCommand,
}
  from "@aws-sdk/lib-dynamodb";

const client = new DynamoDBClient({});

const dynamo = DynamoDBDocumentClient.from(client);

const tableName = "telemetry"; // replace with your table name

export const handler = async (event) => {

  const r = await fetch("https://opendata-download-metobs.smhi.se/api/version/latest/parameter/1/station/72420/period/latest-hour/data.json");
  const rJson = await r.json();

  if (r.status !== 200) {
    throw new Error(`SMHI ${r.status}`);
  }

  console.log(JSON.stringify(rJson))
  try {
    const result = await dynamo.send(
      new PutCommand({
        TableName: tableName,
        Item: {
          "deviceName": rJson.station.key.toString(),
          "timestamp": new Date(rJson.value[0].date).toISOString(),
          "name": rJson.station.name,
          "device_type": "weatherStation",
          "temperature": parseFloat(rJson.value[0].value)
        },
      })
    );

    console.log("DynamoDB result:", result);
  } catch (error) {
    console.error("dynamodb", error);
    throw error;
  }

  return {};
};