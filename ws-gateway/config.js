'use strict';

function integerFromEnv(name, fallback) {
  const value = process.env[name];
  if (value === undefined || value === '') {
    return fallback;
  }

  const parsed = Number.parseInt(value, 10);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

module.exports = {
  websocket: {
    host: process.env.WS_HOST || '127.0.0.1',
    port: integerFromEnv('WS_PORT', 9000),
    maxPayloadBytes: integerFromEnv('WS_MAX_PAYLOAD_BYTES', 64 * 1024)
  },
  tcp: {
    host: process.env.CHAT_TCP_HOST || '127.0.0.1',
    port: integerFromEnv('CHAT_TCP_PORT', 8888),
    maxLineBytes: integerFromEnv('CHAT_TCP_MAX_LINE_BYTES', 256 * 1024)
  },
  gateway: {
    maxQueuedMessages: integerFromEnv('GATEWAY_MAX_QUEUED_MESSAGES', 100)
  }
};
