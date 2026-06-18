module.exports = {
  apps: [
    {
      name: 'chat-ws-gateway',
      cwd: '/opt/aletheia-chat/ws-gateway',
      script: 'server.js',
      env: {
        NODE_ENV: 'production',
        WS_HOST: '127.0.0.1',
        WS_PORT: '9000',
        CHAT_TCP_HOST: '127.0.0.1',
        CHAT_TCP_PORT: '8888'
      },
      autorestart: true,
      max_restarts: 10,
      restart_delay: 3000,
      time: true
    }
  ]
};
