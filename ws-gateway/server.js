'use strict';

const net = require('node:net');
const { StringDecoder } = require('node:string_decoder');
const { WebSocket, WebSocketServer } = require('ws');

const config = require('./config');

let nextConnectionId = 1;

function timestamp() {
  return new Date().toISOString();
}

function log(connectionId, message, details) {
  const suffix = details === undefined ? '' : ` ${JSON.stringify(details)}`;
  console.log(`[${timestamp()}] [connection ${connectionId}] ${message}${suffix}`);
}

function sendJson(ws, object) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(object));
  }
}

function sendGatewayError(ws, message) {
  sendJson(ws, {
    type: 'gateway_error',
    message
  });
}

function closeWebSocket(ws, code, reason) {
  if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
    ws.close(code, reason);
  }
}

function parseBrowserRequest(data, isBinary) {
  if (isBinary) {
    return {
      error: 'binary WebSocket messages are not supported'
    };
  }

  const text = data.toString('utf8').trim();
  if (text === '') {
    return {
      error: 'empty WebSocket message'
    };
  }

  let object;
  try {
    object = JSON.parse(text);
  } catch (error) {
    return {
      error: `invalid JSON: ${error.message}`
    };
  }

  if (object === null || Array.isArray(object) || typeof object !== 'object') {
    return {
      error: 'WebSocket message must be a JSON object'
    };
  }

  if (typeof object.type !== 'string' || object.type.trim() === '') {
    return {
      error: 'JSON object must include a string type field'
    };
  }

  return {
    line: `${JSON.stringify(object)}\n`
  };
}

function attachBridge(ws, request) {
  const connectionId = nextConnectionId++;
  const peer = request.socket.remoteAddress || 'unknown';
  const tcp = net.createConnection({
    host: config.tcp.host,
    port: config.tcp.port
  });
  const decoder = new StringDecoder('utf8');
  const queuedLines = [];

  let tcpReady = false;
  let tcpBuffer = '';
  let wsClosed = false;

  log(connectionId, 'browser connected', { peer });

  function writeToTcp(line) {
    if (Buffer.byteLength(line, 'utf8') > config.tcp.maxLineBytes) {
      sendGatewayError(ws, 'request line is too large');
      return;
    }

    if (!tcpReady) {
      if (queuedLines.length >= config.gateway.maxQueuedMessages) {
        sendGatewayError(ws, 'gateway TCP connection is not ready');
        return;
      }
      queuedLines.push(line);
      return;
    }

    tcp.write(line);
  }

  function flushQueuedLines() {
    while (queuedLines.length > 0 && tcpReady) {
      tcp.write(queuedLines.shift());
    }
  }

  function handleTcpText(text) {
    tcpBuffer += text;

    while (true) {
      const newlineIndex = tcpBuffer.indexOf('\n');
      if (newlineIndex < 0) {
        break;
      }

      let line = tcpBuffer.slice(0, newlineIndex);
      tcpBuffer = tcpBuffer.slice(newlineIndex + 1);
      if (line.endsWith('\r')) {
        line = line.slice(0, -1);
      }

      if (line.trim() !== '' && ws.readyState === WebSocket.OPEN) {
        ws.send(line);
      }
    }

    if (Buffer.byteLength(tcpBuffer, 'utf8') > config.tcp.maxLineBytes) {
      sendGatewayError(ws, 'response line from TCP server is too large');
      tcp.destroy();
      closeWebSocket(ws, 1011, 'TCP response line too large');
    }
  }

  ws.on('message', (data, isBinary) => {
    const parsed = parseBrowserRequest(data, isBinary);
    if (parsed.error) {
      sendGatewayError(ws, parsed.error);
      log(connectionId, 'rejected browser message', { error: parsed.error });
      return;
    }

    writeToTcp(parsed.line);
  });

  ws.on('close', (code, reason) => {
    wsClosed = true;
    log(connectionId, 'browser disconnected', {
      code,
      reason: reason.toString()
    });

    if (!tcp.destroyed) {
      tcp.end();
    }
  });

  ws.on('error', (error) => {
    log(connectionId, 'WebSocket error', { error: error.message });
    if (!tcp.destroyed) {
      tcp.destroy();
    }
  });

  tcp.on('connect', () => {
    tcpReady = true;
    tcp.setKeepAlive(true);
    log(connectionId, 'connected to TCP chat server', {
      host: config.tcp.host,
      port: config.tcp.port
    });
    flushQueuedLines();
  });

  tcp.on('data', (chunk) => {
    handleTcpText(decoder.write(chunk));
  });

  tcp.on('end', () => {
    const rest = decoder.end();
    if (rest !== '') {
      handleTcpText(rest);
    }
  });

  tcp.on('error', (error) => {
    log(connectionId, 'TCP error', { error: error.message });
    sendGatewayError(ws, `TCP chat server error: ${error.message}`);
  });

  tcp.on('close', (hadError) => {
    tcpReady = false;
    log(connectionId, 'TCP connection closed', { hadError });

    if (!wsClosed) {
      closeWebSocket(ws, hadError ? 1011 : 1000, 'TCP chat server connection closed');
    }
  });
}

const wss = new WebSocketServer({
  host: config.websocket.host,
  port: config.websocket.port,
  maxPayload: config.websocket.maxPayloadBytes
});

wss.on('connection', attachBridge);

wss.on('listening', () => {
  console.log(
    `[${timestamp()}] WebSocket gateway listening on ws://${config.websocket.host}:${config.websocket.port}`
  );
  console.log(
    `[${timestamp()}] Forwarding browser sessions to tcp://${config.tcp.host}:${config.tcp.port}`
  );
});

wss.on('error', (error) => {
  console.error(`[${timestamp()}] WebSocket gateway error: ${error.message}`);
});

process.on('SIGINT', () => {
  console.log(`\n[${timestamp()}] Shutting down WebSocket gateway`);
  wss.close(() => {
    process.exit(0);
  });
});
