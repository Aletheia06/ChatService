'use strict';

const SERVER_TYPES = {
  OK: 'ok',
  ERROR: 'error',
  USERS: 'users',
  PRIVATE: 'private',
  ROOM: 'room',
  HISTORY_PRIVATE: 'history_private_result',
  HISTORY_ROOM: 'history_room_result',
  GATEWAY_ERROR: 'gateway_error',
  CALL_INVITE: 'call_invite',
  CALL_ACCEPT: 'call_accept',
  CALL_REJECT: 'call_reject',
  CALL_BUSY: 'call_busy',
  CALL_CANCEL: 'call_cancel',
  CALL_HANGUP: 'call_hangup',
  CALL_TIMEOUT: 'call_timeout',
  CALL_ERROR: 'call_error',
  WEBRTC_OFFER: 'webrtc_offer',
  WEBRTC_ANSWER: 'webrtc_answer',
  ICE_CANDIDATE: 'ice_candidate'
};

const CALL_MESSAGE_TYPES = new Set([
  SERVER_TYPES.CALL_INVITE,
  SERVER_TYPES.CALL_ACCEPT,
  SERVER_TYPES.CALL_REJECT,
  SERVER_TYPES.CALL_BUSY,
  SERVER_TYPES.CALL_CANCEL,
  SERVER_TYPES.CALL_HANGUP,
  SERVER_TYPES.CALL_TIMEOUT,
  SERVER_TYPES.CALL_ERROR,
  SERVER_TYPES.WEBRTC_OFFER,
  SERVER_TYPES.WEBRTC_ANSWER,
  SERVER_TYPES.ICE_CANDIDATE
]);

const CONVERSATION = {
  PRIVATE: 'private',
  ROOM: 'room'
};

const MESSAGE_KIND = {
  MINE: 'mine',
  OTHER: 'other',
  SYSTEM: 'system',
  ERROR: 'error'
};

const CALL_PHASE = {
  IDLE: 'idle',
  OUTGOING: 'outgoing',
  INCOMING: 'incoming',
  CONNECTING: 'connecting',
  IN_CALL: 'in_call',
  FINISHED: 'finished'
};

const state = {
  ws: null,
  username: '',
  pendingUsername: '',
  loggedIn: false,
  current: null,
  users: new Set(),
  rooms: new Set(),
  conversations: new Map(),
  pendingRoomAction: null,
  roomDialogMode: null,
  localMessageSequence: 0,
  call: {
    phase: CALL_PHASE.IDLE,
    callId: '',
    peer: '',
    direction: '',
    timeoutId: null,
    webrtc: null
  }
};

const elements = {
  loginView: document.getElementById('loginView'),
  mainView: document.getElementById('mainView'),
  loginForm: document.getElementById('loginForm'),
  usernameInput: document.getElementById('usernameInput'),
  gatewayUrl: document.getElementById('gatewayUrl'),
  advancedButton: document.getElementById('advancedButton'),
  advancedPanel: document.getElementById('advancedPanel'),
  loginButton: document.getElementById('loginButton'),
  loginStatus: document.getElementById('loginStatus'),
  currentUserLabel: document.getElementById('currentUserLabel'),
  connectionStatus: document.getElementById('connectionStatus'),
  usersList: document.getElementById('usersList'),
  refreshUsersButton: document.getElementById('refreshUsersButton'),
  roomsList: document.getElementById('roomsList'),
  createRoomButton: document.getElementById('createRoomButton'),
  joinRoomButton: document.getElementById('joinRoomButton'),
  leaveRoomButton: document.getElementById('leaveRoomButton'),
  recentList: document.getElementById('recentList'),
  chatTitle: document.getElementById('chatTitle'),
  messages: document.getElementById('messages'),
  messageForm: document.getElementById('messageForm'),
  messageInput: document.getElementById('messageInput'),
  sendButton: document.getElementById('sendButton'),
  actionStatus: document.getElementById('actionStatus'),
  roomDialog: document.getElementById('roomDialog'),
  roomDialogForm: document.getElementById('roomDialogForm'),
  roomDialogTitle: document.getElementById('roomDialogTitle'),
  roomDialogInput: document.getElementById('roomDialogInput'),
  roomDialogCancel: document.getElementById('roomDialogCancel'),
  callDialog: document.getElementById('callDialog'),
  callDialogTitle: document.getElementById('callDialogTitle'),
  callPeerLabel: document.getElementById('callPeerLabel'),
  callVideoStage: document.getElementById('callVideoStage'),
  callStatus: document.getElementById('callStatus'),
  incomingCallActions: document.getElementById('incomingCallActions'),
  outgoingCallActions: document.getElementById('outgoingCallActions'),
  activeCallActions: document.getElementById('activeCallActions'),
  finishedCallActions: document.getElementById('finishedCallActions'),
  acceptCallButton: document.getElementById('acceptCallButton'),
  rejectCallButton: document.getElementById('rejectCallButton'),
  cancelCallButton: document.getElementById('cancelCallButton'),
  muteCallButton: document.getElementById('muteCallButton'),
  cameraCallButton: document.getElementById('cameraCallButton'),
  hangupCallButton: document.getElementById('hangupCallButton'),
  dismissCallButton: document.getElementById('dismissCallButton'),
  localVideo: document.getElementById('localVideo'),
  remoteVideo: document.getElementById('remoteVideo')
};

function defaultGatewayUrl() {
  if (window.location.protocol === 'http:' || window.location.protocol === 'https:') {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${protocol}//${window.location.host}/ws`;
  }
  return 'ws://localhost:9000';
}

function setLoginStatus(text, isError = false) {
  elements.loginStatus.textContent = text;
  elements.loginStatus.classList.toggle('error', isError);
}

function setConnectionStatus(text, mode) {
  elements.connectionStatus.textContent = text;
  elements.connectionStatus.className = `connection-status ${mode}`;
}

function setActionStatus(text, isError = false) {
  elements.actionStatus.textContent = text;
  elements.actionStatus.classList.toggle('error', isError);
}

function setLoginEnabled(enabled) {
  elements.loginButton.disabled = !enabled;
  elements.advancedButton.disabled = !enabled;
  elements.usernameInput.disabled = !enabled;
  elements.gatewayUrl.disabled = !enabled;
}

function isSocketOpen() {
  return state.ws !== null && state.ws.readyState === WebSocket.OPEN;
}

function isActiveConversation(type, id) {
  return state.current !== null && state.current.type === type && state.current.id === id;
}

function conversationKey(type, id) {
  return `${type}:${id}`;
}

function nextLocalMessageKey() {
  state.localMessageSequence += 1;
  return `local:${state.localMessageSequence}`;
}

function getConversation(type, id) {
  const key = conversationKey(type, id);
  if (!state.conversations.has(key)) {
    state.conversations.set(key, {
      type,
      id,
      messages: [],
      messageKeys: new Set(),
      historyRequested: false,
      unreadCount: 0
    });
  }
  return state.conversations.get(key);
}

function conversationTitle(type, id) {
  if (type === CONVERSATION.PRIVATE) {
    return `Private chat with ${id}`;
  }
  if (type === CONVERSATION.ROOM) {
    return `# ${id}`;
  }
  return 'Select a user or room';
}

function recentLabel(conversation) {
  const prefix = conversation.unreadCount > 0 ? `(${conversation.unreadCount}) ` : '';
  return `${prefix}${conversation.type === CONVERSATION.PRIVATE ? `Private: ${conversation.id}` : `# ${conversation.id}`}`;
}

function userItemText(username) {
  const conversation = state.conversations.get(conversationKey(CONVERSATION.PRIVATE, username));
  const prefix = conversation && conversation.unreadCount > 0 ? `(${conversation.unreadCount}) ` : '';
  return `${prefix}@ ${username}`;
}

function roomItemText(room) {
  const conversation = state.conversations.get(conversationKey(CONVERSATION.ROOM, room));
  const prefix = conversation && conversation.unreadCount > 0 ? `(${conversation.unreadCount}) ` : '';
  return `${prefix}# ${room}`;
}

function formatTime(createdAt) {
  const numeric = Number(createdAt);
  const date = Number.isFinite(numeric) && numeric > 0 ? new Date(numeric * 1000) : new Date();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hour = String(date.getHours()).padStart(2, '0');
  const minute = String(date.getMinutes()).padStart(2, '0');
  return `${month}-${day} ${hour}:${minute}`;
}

function setMainControls() {
  const canChat = state.loggedIn && state.current !== null;
  elements.messageInput.disabled = !canChat;
  elements.sendButton.disabled = !canChat;
  elements.refreshUsersButton.disabled = !state.loggedIn;
  elements.createRoomButton.disabled = !state.loggedIn;
  elements.joinRoomButton.disabled = !state.loggedIn;
  elements.leaveRoomButton.disabled =
    !state.loggedIn || state.current === null || state.current.type !== CONVERSATION.ROOM;
}

function renderList(container, items, getText, type) {
  container.innerHTML = '';

  const values = Array.from(items).sort((a, b) => a.localeCompare(b));
  if (values.length === 0) {
    container.className = 'qt-list empty';
    container.textContent = '';
    return;
  }

  container.className = 'qt-list';
  for (const value of values) {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'list-item';
    item.textContent = getText(value);
    item.setAttribute('role', 'option');
    if (isActiveConversation(type, value)) {
      item.classList.add('active');
      item.setAttribute('aria-selected', 'true');
    }
    item.addEventListener('click', () => selectConversation(type, value));
    container.appendChild(item);
  }
}

function renderUsers() {
  elements.usersList.innerHTML = '';

  const users = Array.from(state.users).sort((a, b) => a.localeCompare(b));
  if (users.length === 0) {
    elements.usersList.className = 'qt-list empty';
    return;
  }

  elements.usersList.className = 'qt-list';
  const callBusy = hasCurrentCall();
  for (const username of users) {
    const row = document.createElement('div');
    row.className = 'user-list-row';

    const chatButton = document.createElement('button');
    chatButton.type = 'button';
    chatButton.className = 'list-item user-chat-button';
    chatButton.textContent = userItemText(username);
    chatButton.setAttribute('role', 'option');
    if (isActiveConversation(CONVERSATION.PRIVATE, username)) {
      chatButton.classList.add('active');
      chatButton.setAttribute('aria-selected', 'true');
    }
    chatButton.addEventListener('click', () => {
      selectConversation(CONVERSATION.PRIVATE, username);
    });

    const callButton = document.createElement('button');
    callButton.type = 'button';
    callButton.className = 'video-call-button';
    callButton.textContent = 'Video Call';
    callButton.disabled = !state.loggedIn || callBusy || username === state.username;
    callButton.title = callBusy ? 'Finish the current call first' : `Call ${username}`;
    callButton.addEventListener('click', () => startVideoCall(username));

    row.append(chatButton, callButton);
    elements.usersList.appendChild(row);
  }
}

function renderRooms() {
  renderList(elements.roomsList, state.rooms, roomItemText, CONVERSATION.ROOM);
}

function renderRecent() {
  elements.recentList.innerHTML = '';

  const conversations = Array.from(state.conversations.values())
    .filter((conversation) => conversation.type === CONVERSATION.PRIVATE || conversation.type === CONVERSATION.ROOM)
    .sort((left, right) => {
      const leftTime = left.messages.length > 0 ? Number(left.messages[left.messages.length - 1].createdAt || 0) : 0;
      const rightTime = right.messages.length > 0 ? Number(right.messages[right.messages.length - 1].createdAt || 0) : 0;
      return rightTime - leftTime;
    });

  if (conversations.length === 0) {
    elements.recentList.className = 'qt-list empty';
    elements.recentList.textContent = '';
    return;
  }

  elements.recentList.className = 'qt-list';
  for (const conversation of conversations) {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'list-item';
    item.textContent = recentLabel(conversation);
    item.setAttribute('role', 'option');
    if (isActiveConversation(conversation.type, conversation.id)) {
      item.classList.add('active');
      item.setAttribute('aria-selected', 'true');
    }
    item.addEventListener('click', () => selectConversation(conversation.type, conversation.id));
    elements.recentList.appendChild(item);
  }
}

function renderMessages() {
  elements.messages.innerHTML = '';

  if (state.current === null) {
    elements.chatTitle.textContent = 'Select a user or room';
    setMainControls();
    return;
  }

  const conversation = getConversation(state.current.type, state.current.id);
  elements.chatTitle.textContent = conversationTitle(state.current.type, state.current.id);

  for (const message of conversation.messages) {
    appendBubble(message);
  }

  elements.messages.scrollTop = elements.messages.scrollHeight;
  setMainControls();
}

function appendBubble(message) {
  const row = document.createElement('article');
  row.className = `message-row ${message.kind}`;

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble';

  const sender = document.createElement('div');
  sender.className = 'message-sender';
  sender.textContent = message.sender || '';

  const text = document.createElement('div');
  text.className = 'message-text';
  text.textContent = message.content || '';

  const time = document.createElement('div');
  time.className = 'message-time';
  time.textContent = formatTime(message.createdAt);

  bubble.append(sender, text, time);
  row.appendChild(bubble);
  elements.messages.appendChild(row);
}

function refreshAllLists() {
  renderUsers();
  renderRooms();
  renderRecent();
}

function addMessageToConversation(type, id, message, renderIfActive, markUnread) {
  const conversation = getConversation(type, id);
  const stored = {
    ...message,
    createdAt: message.createdAt || Math.floor(Date.now() / 1000),
    dedupeKey: message.dedupeKey || (message.id ? `db:${message.id}` : nextLocalMessageKey())
  };

  if (conversation.messageKeys.has(stored.dedupeKey)) {
    return false;
  }

  conversation.messageKeys.add(stored.dedupeKey);
  conversation.messages.push(stored);
  conversation.messages.sort((left, right) => {
    const leftTime = Number(left.createdAt || 0);
    const rightTime = Number(right.createdAt || 0);
    if (leftTime !== rightTime) {
      return leftTime - rightTime;
    }
    return String(left.dedupeKey).localeCompare(String(right.dedupeKey));
  });

  const active = isActiveConversation(type, id);
  if (!active && markUnread) {
    conversation.unreadCount += 1;
  }

  refreshAllLists();
  if (active && renderIfActive) {
    renderMessages();
  }
  return true;
}

function appendSystemMessage(text) {
  if (state.current === null) {
    setActionStatus(text);
    return;
  }

  addMessageToConversation(state.current.type, state.current.id, {
    sender: 'System',
    content: text,
    kind: MESSAGE_KIND.SYSTEM
  }, true, false);
  setActionStatus(text);
}

function appendErrorMessage(text) {
  if (state.current === null) {
    setActionStatus(`Error: ${text}`, true);
    return;
  }

  addMessageToConversation(state.current.type, state.current.id, {
    sender: 'Error',
    content: text,
    kind: MESSAGE_KIND.ERROR
  }, true, false);
  setActionStatus(`Error: ${text}`, true);
}

function sendMessage(type, payload = {}) {
  if (!isSocketOpen()) {
    appendErrorMessage('WebSocket is not connected');
    return false;
  }

  state.ws.send(JSON.stringify({ type, ...payload }));
  return true;
}

function hasCurrentCall() {
  return state.call.phase !== CALL_PHASE.IDLE &&
    state.call.phase !== CALL_PHASE.FINISHED;
}

function generateCallId() {
  if (window.crypto && typeof window.crypto.randomUUID === 'function') {
    return window.crypto.randomUUID();
  }
  return `${state.username}-${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function clearCallTimeout() {
  if (state.call.timeoutId !== null) {
    window.clearTimeout(state.call.timeoutId);
    state.call.timeoutId = null;
  }
}

function startMediaConnectionTimeout() {
  clearCallTimeout();
  const callId = state.call.callId;
  state.call.timeoutId = window.setTimeout(() => {
    if (state.call.phase === CALL_PHASE.CONNECTING &&
        state.call.callId === callId) {
      sendCurrentCallMessage(SERVER_TYPES.CALL_HANGUP);
      finishCall('Media connection timed out.', true);
    }
  }, 30000);
}

function setCallStatus(text, isError = false) {
  elements.callStatus.textContent = text;
  elements.callStatus.classList.toggle('error', isError);
}

function renderCallUi() {
  const phase = state.call.phase;
  if (phase === CALL_PHASE.IDLE) {
    elements.callDialog.hidden = true;
    return;
  }

  elements.callDialog.hidden = false;
  elements.callPeerLabel.textContent = state.call.peer ? `@ ${state.call.peer}` : '';
  elements.incomingCallActions.hidden = phase !== CALL_PHASE.INCOMING;
  elements.outgoingCallActions.hidden = phase !== CALL_PHASE.OUTGOING;
  elements.activeCallActions.hidden =
    phase !== CALL_PHASE.CONNECTING && phase !== CALL_PHASE.IN_CALL;
  elements.finishedCallActions.hidden = phase !== CALL_PHASE.FINISHED;
  elements.callVideoStage.hidden =
    phase !== CALL_PHASE.CONNECTING && phase !== CALL_PHASE.IN_CALL;

  elements.acceptCallButton.disabled = false;
  elements.rejectCallButton.disabled = false;

  if (phase === CALL_PHASE.INCOMING) {
    elements.callDialogTitle.textContent = 'Incoming video call';
    setCallStatus(`${state.call.peer} is calling you`);
  } else if (phase === CALL_PHASE.OUTGOING) {
    elements.callDialogTitle.textContent = 'Calling';
    setCallStatus(`Calling ${state.call.peer}...`);
  } else if (phase === CALL_PHASE.CONNECTING) {
    elements.callDialogTitle.textContent = 'Video call';
    setCallStatus('Connecting media...');
  } else if (phase === CALL_PHASE.IN_CALL) {
    elements.callDialogTitle.textContent = 'Video call';
    setCallStatus('Connected');
  } else if (phase === CALL_PHASE.FINISHED) {
    elements.callDialogTitle.textContent = 'Video call ended';
  }
}

function ensureWebRtc() {
  if (state.call.webrtc) {
    return state.call.webrtc;
  }

  state.call.webrtc = new window.ChatWebRTC.WebRtcCall({
    localVideo: elements.localVideo,
    remoteVideo: elements.remoteVideo,
    sendSignal: (type, payload) => {
      sendCurrentCallMessage(type, payload);
    },
    onStatus: (text) => {
      if (text === 'Connected' && hasCurrentCall()) {
        clearCallTimeout();
        state.call.phase = CALL_PHASE.IN_CALL;
        renderCallUi();
      } else {
        setCallStatus(text);
      }
    },
    onFailure: (message) => {
      handleLocalCallFailure(message);
    }
  });
  return state.call.webrtc;
}

function cleanupWebRtc() {
  if (state.call.webrtc) {
    state.call.webrtc.close();
    state.call.webrtc = null;
  }
}

function resetCallState() {
  clearCallTimeout();
  cleanupWebRtc();
  state.call.phase = CALL_PHASE.IDLE;
  state.call.callId = '';
  state.call.peer = '';
  state.call.direction = '';
  elements.muteCallButton.textContent = 'Mute microphone';
  elements.cameraCallButton.textContent = 'Turn camera off';
  renderCallUi();
  renderUsers();
}

function finishCall(message, isError = false) {
  clearCallTimeout();
  cleanupWebRtc();
  state.call.phase = CALL_PHASE.FINISHED;
  renderCallUi();
  setCallStatus(message, isError);
  renderUsers();
  setActionStatus(message, isError);
}

function callReasonMessage(reason) {
  const messages = {
    rejected: 'Call rejected',
    target_busy: 'User is busy',
    caller_busy: 'You are already busy',
    target_offline: 'User is offline',
    peer_disconnected: 'The other user disconnected',
    media_unavailable: 'The other user could not access camera or microphone',
    timeout: 'Call timed out',
    cannot_call_self: 'You cannot call yourself',
    invalid_pending_call: 'The pending call is no longer valid',
    invalid_active_call: 'The active call is no longer valid',
    not_in_same_active_call: 'WebRTC signaling was rejected for this call'
  };
  return messages[reason] || `Call failed: ${reason || 'unknown error'}`;
}

function sendCurrentCallMessage(type, payload = {}) {
  if (!state.call.callId || !state.call.peer) {
    return false;
  }
  return sendMessage(type, {
    to: state.call.peer,
    call_id: state.call.callId,
    ...payload
  });
}

function startVideoCall(peer) {
  if (!state.loggedIn || !isSocketOpen()) {
    setActionStatus('Connect before starting a video call.', true);
    return;
  }
  if (peer === state.username) {
    setActionStatus('You cannot call yourself.', true);
    return;
  }
  if (hasCurrentCall()) {
    setActionStatus('Finish the current call first.', true);
    return;
  }
  if (state.call.phase === CALL_PHASE.FINISHED) {
    resetCallState();
  }

  state.call.phase = CALL_PHASE.OUTGOING;
  state.call.callId = generateCallId();
  state.call.peer = peer;
  state.call.direction = 'outgoing';
  elements.muteCallButton.textContent = 'Mute microphone';
  elements.cameraCallButton.textContent = 'Turn camera off';
  renderCallUi();
  renderUsers();

  if (!sendCurrentCallMessage(SERVER_TYPES.CALL_INVITE, { media: 'video' })) {
    finishCall('Could not send the call invitation.', true);
    return;
  }

  const callId = state.call.callId;
  state.call.timeoutId = window.setTimeout(() => {
    if (state.call.phase === CALL_PHASE.OUTGOING &&
        state.call.callId === callId) {
      sendCurrentCallMessage(SERVER_TYPES.CALL_TIMEOUT);
      finishCall('No answer. Call timed out.');
    }
  }, 30000);
}

async function acceptIncomingCall() {
  if (state.call.phase !== CALL_PHASE.INCOMING) {
    return;
  }

  const callId = state.call.callId;
  elements.acceptCallButton.disabled = true;
  elements.rejectCallButton.disabled = true;
  setCallStatus('Requesting camera and microphone...');

  try {
    const webrtc = ensureWebRtc();
    await webrtc.prepareMedia();
    if (state.call.phase !== CALL_PHASE.INCOMING ||
        state.call.callId !== callId) {
      cleanupWebRtc();
      return;
    }
    state.call.phase = CALL_PHASE.CONNECTING;
    renderCallUi();
    startMediaConnectionTimeout();
    if (!sendCurrentCallMessage(SERVER_TYPES.CALL_ACCEPT)) {
      finishCall('Could not accept the call because signaling disconnected.', true);
    }
  } catch (error) {
    if (state.call.phase === CALL_PHASE.INCOMING &&
        state.call.callId === callId) {
      sendCurrentCallMessage(SERVER_TYPES.CALL_REJECT, {
        reason: 'media_unavailable'
      });
      finishCall(error.message, true);
    }
  }
}

function rejectIncomingCall() {
  if (state.call.phase !== CALL_PHASE.INCOMING) {
    return;
  }
  sendCurrentCallMessage(SERVER_TYPES.CALL_REJECT, { reason: 'rejected' });
  finishCall('Call rejected');
}

function cancelOutgoingCall() {
  if (state.call.phase !== CALL_PHASE.OUTGOING) {
    return;
  }
  sendCurrentCallMessage(SERVER_TYPES.CALL_CANCEL);
  finishCall('Call cancelled');
}

function hangupActiveCall() {
  if (state.call.phase !== CALL_PHASE.CONNECTING &&
      state.call.phase !== CALL_PHASE.IN_CALL) {
    return;
  }
  sendCurrentCallMessage(SERVER_TYPES.CALL_HANGUP);
  finishCall('Call ended');
}

function handleLocalCallFailure(message) {
  if (!hasCurrentCall()) {
    return;
  }
  if (state.call.phase === CALL_PHASE.INCOMING) {
    sendCurrentCallMessage(SERVER_TYPES.CALL_REJECT, {
      reason: 'media_unavailable'
    });
  } else if (state.call.phase === CALL_PHASE.CONNECTING ||
             state.call.phase === CALL_PHASE.IN_CALL) {
    sendCurrentCallMessage(SERVER_TYPES.CALL_HANGUP);
  } else {
    sendCurrentCallMessage(SERVER_TYPES.CALL_CANCEL);
  }
  finishCall(message, true);
}

async function handleCallProtocolMessage(message) {
  const type = message.type;
  const callId = message.call_id || '';
  const peer = message.from || '';

  if (type === SERVER_TYPES.CALL_INVITE) {
    if (!peer || !callId) {
      return;
    }
    if (hasCurrentCall()) {
      sendMessage(SERVER_TYPES.CALL_REJECT, {
        to: peer,
        call_id: callId,
        reason: 'busy'
      });
      return;
    }
    if (state.call.phase === CALL_PHASE.FINISHED) {
      resetCallState();
    }
    state.call.phase = CALL_PHASE.INCOMING;
    state.call.callId = callId;
    state.call.peer = peer;
    state.call.direction = 'incoming';
    elements.muteCallButton.textContent = 'Mute microphone';
    elements.cameraCallButton.textContent = 'Turn camera off';
    renderCallUi();
    renderUsers();
    return;
  }

  if (!callId || callId !== state.call.callId) {
    return;
  }

  if (type === SERVER_TYPES.CALL_ACCEPT) {
    if (state.call.phase !== CALL_PHASE.OUTGOING) {
      return;
    }
    clearCallTimeout();
    state.call.phase = CALL_PHASE.CONNECTING;
    renderCallUi();
    startMediaConnectionTimeout();
    try {
      await ensureWebRtc().startAsCaller();
    } catch (error) {
      handleLocalCallFailure(error.message);
    }
    return;
  }

  if (type === SERVER_TYPES.WEBRTC_OFFER) {
    if (state.call.phase !== CALL_PHASE.CONNECTING &&
        state.call.phase !== CALL_PHASE.IN_CALL) {
      return;
    }
    try {
      await ensureWebRtc().receiveOffer(message.sdp);
    } catch (error) {
      handleLocalCallFailure(`Could not accept WebRTC offer: ${error.message}`);
    }
    return;
  }

  if (type === SERVER_TYPES.WEBRTC_ANSWER) {
    if (state.call.phase !== CALL_PHASE.CONNECTING &&
        state.call.phase !== CALL_PHASE.IN_CALL) {
      return;
    }
    try {
      await ensureWebRtc().receiveAnswer(message.sdp);
    } catch (error) {
      handleLocalCallFailure(`Could not apply WebRTC answer: ${error.message}`);
    }
    return;
  }

  if (type === SERVER_TYPES.ICE_CANDIDATE) {
    if (state.call.phase !== CALL_PHASE.CONNECTING &&
        state.call.phase !== CALL_PHASE.IN_CALL) {
      return;
    }
    try {
      await ensureWebRtc().addIceCandidate(message.candidate);
    } catch (error) {
      handleLocalCallFailure(`Could not add ICE candidate: ${error.message}`);
    }
    return;
  }

  if (type === SERVER_TYPES.CALL_REJECT) {
    finishCall(callReasonMessage(message.reason || 'rejected'),
               message.reason === 'media_unavailable');
  } else if (type === SERVER_TYPES.CALL_BUSY) {
    finishCall(callReasonMessage(message.reason || 'target_busy'));
  } else if (type === SERVER_TYPES.CALL_CANCEL) {
    finishCall(message.reason === 'peer_disconnected'
      ? 'Caller disconnected'
      : 'Caller cancelled the call');
  } else if (type === SERVER_TYPES.CALL_TIMEOUT) {
    finishCall('Call timed out');
  } else if (type === SERVER_TYPES.CALL_HANGUP) {
    finishCall(message.reason === 'peer_disconnected'
      ? 'The other user disconnected'
      : 'The other user ended the call');
  } else if (type === SERVER_TYPES.CALL_ERROR) {
    finishCall(callReasonMessage(message.reason), true);
  }
}

function requestHistoryIfNeeded(conversation) {
  if (conversation.historyRequested || !state.loggedIn) {
    return;
  }

  conversation.historyRequested = true;
  if (conversation.type === CONVERSATION.PRIVATE) {
    sendMessage('history_private', {
      peer: conversation.id,
      limit: 50
    });
    setActionStatus('Loading private history...');
  } else if (conversation.type === CONVERSATION.ROOM) {
    sendMessage('history_room', {
      room: conversation.id,
      limit: 50
    });
    setActionStatus('Loading room history...');
  }
}

function selectConversation(type, id) {
  if (!id) {
    state.current = null;
    elements.chatTitle.textContent = 'Select a user or room';
    renderMessages();
    setActionStatus('Select a conversation.');
    return;
  }

  const conversation = getConversation(type, id);
  conversation.unreadCount = 0;
  state.current = { type, id };

  if (type === CONVERSATION.PRIVATE) {
    state.users.add(id);
  } else if (type === CONVERSATION.ROOM) {
    state.rooms.add(id);
  }

  refreshAllLists();
  requestHistoryIfNeeded(conversation);
  renderMessages();
  setActionStatus(conversationTitle(type, id));
  elements.messageInput.focus();
}

function connectAndLogin(username) {
  const gateway = elements.gatewayUrl.value.trim();
  if (!gateway) {
    setLoginStatus('Gateway is required.', true);
    return;
  }

  state.pendingUsername = username;
  setLoginEnabled(false);
  setLoginStatus('Connecting...');

  if (state.ws !== null) {
    state.ws.close(1000, 'Reconnect for login');
    state.ws = null;
  }

  state.ws = new WebSocket(gateway);

  state.ws.addEventListener('open', () => {
    setLoginStatus('Connected. Logging in...');
    sendMessage('login', { username: state.pendingUsername });
  });

  state.ws.addEventListener('message', (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch (error) {
      handleLoginOrMainError(`Invalid JSON from gateway: ${error.message}`);
      return;
    }
    handleServerMessage(message);
  });

  state.ws.addEventListener('close', () => {
    const wasLoggedIn = state.loggedIn;
    state.loggedIn = false;
    state.pendingUsername = '';
    if (hasCurrentCall()) {
      finishCall('Disconnected from server. The call was ended.', true);
    } else {
      cleanupWebRtc();
    }
    setConnectionStatus('Disconnected', 'disconnected');
    setMainControls();

    if (!wasLoggedIn) {
      setLoginEnabled(true);
      setLoginStatus('Disconnected from server.', true);
    } else {
      appendSystemMessage('Disconnected from server.');
    }
  });

  state.ws.addEventListener('error', () => {
    handleLoginOrMainError('Connection error.');
  });
}

function handleLoginSubmit(event) {
  event.preventDefault();

  const username = elements.usernameInput.value.trim();
  if (!username) {
    setLoginStatus('Username is required.', true);
    return;
  }

  connectAndLogin(username);
}

function showMainWindow(username) {
  state.username = username;
  state.loggedIn = true;
  state.pendingUsername = '';
  document.title = `Chat Client - ${username}`;
  elements.currentUserLabel.textContent = username;
  elements.loginView.hidden = true;
  elements.mainView.hidden = false;
  setConnectionStatus('Connected', 'connected');
  setActionStatus(`Connected as ${username}`);
  setMainControls();
  sendMessage('users');
}

function handleLoginOrMainError(message) {
  if (!state.loggedIn) {
    setLoginStatus(message, true);
    setLoginEnabled(true);
  } else {
    appendErrorMessage(message);
  }
}

function handleServerMessage(message) {
  const type = message.type;

  if (CALL_MESSAGE_TYPES.has(type)) {
    handleCallProtocolMessage(message);
    return;
  }

  if (type === SERVER_TYPES.OK) {
    handleOk(message);
    return;
  }

  if (type === SERVER_TYPES.ERROR || type === SERVER_TYPES.GATEWAY_ERROR) {
    const text = message.message || 'Unknown error';
    state.pendingRoomAction = null;
    handleLoginOrMainError(text);
    return;
  }

  if (type === SERVER_TYPES.USERS) {
    handleUsers(message.users || '');
    return;
  }

  if (type === SERVER_TYPES.PRIVATE) {
    handlePrivateMessage(message);
    return;
  }

  if (type === SERVER_TYPES.ROOM) {
    handleRoomMessage(message);
    return;
  }

  if (type === SERVER_TYPES.HISTORY_PRIVATE) {
    handlePrivateHistory(message);
    return;
  }

  if (type === SERVER_TYPES.HISTORY_ROOM) {
    handleRoomHistory(message);
    return;
  }

  appendErrorMessage(`Unknown server message type: ${type || '(missing)'}`);
}

function handleOk(message) {
  const text = message.message || '';

  if (text === 'connected' && !state.loggedIn) {
    return;
  }

  if (text.startsWith('logged in as ')) {
    showMainWindow(text.slice('logged in as '.length));
    return;
  }

  if (text === 'logged out') {
    state.loggedIn = false;
    state.username = '';
    appendSystemMessage('Logged out.');
    setMainControls();
    return;
  }

  if (text === 'room created' && state.pendingRoomAction !== null) {
    state.pendingRoomAction.mode = 'join';
    setActionStatus('Room created. Joining...');
    sendMessage('join', { room: state.pendingRoomAction.room });
    return;
  }

  if (text === 'joined room' && state.pendingRoomAction !== null) {
    const room = state.pendingRoomAction.room;
    state.rooms.add(room);
    state.pendingRoomAction = null;
    selectConversation(CONVERSATION.ROOM, room);
    appendSystemMessage(`Joined room: ${room}`);
    return;
  }

  if (text === 'left room' && state.pendingRoomAction !== null) {
    const room = state.pendingRoomAction.room;
    state.rooms.delete(room);
    state.pendingRoomAction = null;
    if (isActiveConversation(CONVERSATION.ROOM, room)) {
      state.current = null;
      renderMessages();
    }
    refreshAllLists();
    setActionStatus(`Left room: ${room}`);
    return;
  }

  if (text === 'private message sent' || text === 'private message saved') {
    return;
  }

  if (text) {
    appendSystemMessage(text);
  }
}

function handleUsers(usersText) {
  state.users.clear();
  usersText
    .split(',')
    .map((part) => part.trim())
    .filter((user) => user && user !== state.username)
    .forEach((user) => state.users.add(user));
  refreshAllLists();
  setActionStatus('Online users refreshed.');
}

function handlePrivateMessage(message) {
  const from = message.from || '';
  if (!from) {
    return;
  }

  state.users.add(from);
  addMessageToConversation(CONVERSATION.PRIVATE, from, {
    id: message.id,
    sender: from,
    content: message.message || '',
    createdAt: message.created_at,
    kind: MESSAGE_KIND.OTHER
  }, true, true);
}

function handleRoomMessage(message) {
  const room = message.room || '';
  const from = message.from || '';
  if (!room || !from) {
    return;
  }

  if (from === state.username) {
    return;
  }

  state.rooms.add(room);
  addMessageToConversation(CONVERSATION.ROOM, room, {
    id: message.id,
    sender: from,
    content: message.message || '',
    createdAt: message.created_at,
    kind: MESSAGE_KIND.OTHER
  }, true, true);
}

function handlePrivateHistory(message) {
  const peer = message.peer || '';
  if (!peer || !Array.isArray(message.messages)) {
    appendErrorMessage('Invalid private history response');
    return;
  }

  let added = 0;
  for (const item of message.messages) {
    const sender = item.sender || '';
    const content = item.content || '';
    if (!sender || !content) {
      continue;
    }
    const mine = sender === state.username;
    if (addMessageToConversation(CONVERSATION.PRIVATE, peer, {
      id: item.id,
      sender: mine ? 'Me' : sender,
      content,
      createdAt: item.created_at,
      kind: mine ? MESSAGE_KIND.MINE : MESSAGE_KIND.OTHER
    }, false, false)) {
      added += 1;
    }
  }

  if (isActiveConversation(CONVERSATION.PRIVATE, peer)) {
    renderMessages();
  }
  setActionStatus(`Loaded ${added} private history message(s).`);
}

function handleRoomHistory(message) {
  const room = message.room || '';
  if (!room || !Array.isArray(message.messages)) {
    appendErrorMessage('Invalid room history response');
    return;
  }

  let added = 0;
  state.rooms.add(room);
  for (const item of message.messages) {
    const sender = item.sender || '';
    const content = item.content || '';
    if (!sender || !content) {
      continue;
    }
    const mine = sender === state.username;
    if (addMessageToConversation(CONVERSATION.ROOM, room, {
      id: item.id,
      sender: mine ? 'Me' : sender,
      content,
      createdAt: item.created_at,
      kind: mine ? MESSAGE_KIND.MINE : MESSAGE_KIND.OTHER
    }, false, false)) {
      added += 1;
    }
  }

  if (isActiveConversation(CONVERSATION.ROOM, room)) {
    renderMessages();
  }
  setActionStatus(`Loaded ${added} room history message(s).`);
}

function sendCurrentMessage(event) {
  event.preventDefault();

  if (state.current === null) {
    setActionStatus('Select an online user or room first.');
    return;
  }

  const text = elements.messageInput.value.trim();
  if (!text) {
    setActionStatus('Type a message before sending.');
    return;
  }

  if (state.current.type === CONVERSATION.PRIVATE) {
    const target = state.current.id;
    if (!sendMessage('private', { target, message: text })) {
      return;
    }
    addMessageToConversation(CONVERSATION.PRIVATE, target, {
      sender: `Me -> ${target}`,
      content: text,
      createdAt: Math.floor(Date.now() / 1000),
      kind: MESSAGE_KIND.MINE
    }, true, false);
  } else if (state.current.type === CONVERSATION.ROOM) {
    const room = state.current.id;
    if (!sendMessage('room_msg', { room, message: text })) {
      return;
    }
    addMessageToConversation(CONVERSATION.ROOM, room, {
      sender: 'Me',
      content: text,
      createdAt: Math.floor(Date.now() / 1000),
      kind: MESSAGE_KIND.MINE
    }, true, false);
  }

  elements.messageInput.value = '';
  elements.messageInput.focus();
  setActionStatus('Message sent.');
}

function openRoomDialog(mode) {
  state.roomDialogMode = mode;
  elements.roomDialogTitle.textContent = mode === 'create' ? 'Create Room' : 'Join Room';
  elements.roomDialogInput.value = '';
  elements.roomDialog.hidden = false;
  elements.roomDialogInput.focus();
}

function closeRoomDialog() {
  state.roomDialogMode = null;
  elements.roomDialog.hidden = true;
}

function submitRoomDialog(event) {
  event.preventDefault();

  const room = elements.roomDialogInput.value.trim();
  if (!room) {
    return;
  }

  const mode = state.roomDialogMode;
  closeRoomDialog();

  state.pendingRoomAction = { room, mode };
  if (mode === 'create') {
    setActionStatus(`Creating room ${room}...`);
    sendMessage('create_room', { room });
  } else {
    setActionStatus(`Joining room ${room}...`);
    sendMessage('join', { room });
  }
}

function leaveCurrentRoom() {
  if (state.current === null || state.current.type !== CONVERSATION.ROOM) {
    setActionStatus('Select a room first.');
    appendSystemMessage('Select a room before leaving.');
    return;
  }

  state.pendingRoomAction = { room: state.current.id, mode: 'leave' };
  setActionStatus(`Leaving room ${state.current.id}...`);
  sendMessage('leave', { room: state.current.id });
}

function toggleAdvancedSettings() {
  const expanded = elements.advancedPanel.hidden;
  elements.advancedPanel.hidden = !expanded;
  elements.advancedButton.textContent = expanded ? 'Hide Advanced Settings' : 'Advanced Settings';
  elements.advancedButton.setAttribute('aria-expanded', String(expanded));
}

elements.gatewayUrl.value = defaultGatewayUrl();
elements.loginForm.addEventListener('submit', handleLoginSubmit);
elements.advancedButton.addEventListener('click', toggleAdvancedSettings);
elements.refreshUsersButton.addEventListener('click', () => {
  setActionStatus('Refreshing online users...');
  sendMessage('users');
});
elements.createRoomButton.addEventListener('click', () => openRoomDialog('create'));
elements.joinRoomButton.addEventListener('click', () => openRoomDialog('join'));
elements.leaveRoomButton.addEventListener('click', leaveCurrentRoom);
elements.messageForm.addEventListener('submit', sendCurrentMessage);
elements.roomDialogForm.addEventListener('submit', submitRoomDialog);
elements.roomDialogCancel.addEventListener('click', closeRoomDialog);
elements.roomDialog.addEventListener('click', (event) => {
  if (event.target === elements.roomDialog) {
    closeRoomDialog();
  }
});
elements.acceptCallButton.addEventListener('click', acceptIncomingCall);
elements.rejectCallButton.addEventListener('click', rejectIncomingCall);
elements.cancelCallButton.addEventListener('click', cancelOutgoingCall);
elements.hangupCallButton.addEventListener('click', hangupActiveCall);
elements.dismissCallButton.addEventListener('click', resetCallState);
elements.muteCallButton.addEventListener('click', () => {
  if (!state.call.webrtc) {
    return;
  }
  const enabled = state.call.webrtc.toggleMicrophone();
  elements.muteCallButton.textContent = enabled ? 'Mute microphone' : 'Unmute microphone';
});
elements.cameraCallButton.addEventListener('click', () => {
  if (!state.call.webrtc) {
    return;
  }
  const enabled = state.call.webrtc.toggleCamera();
  elements.cameraCallButton.textContent = enabled ? 'Turn camera off' : 'Turn camera on';
});

window.addEventListener('beforeunload', () => {
  if (!hasCurrentCall() || !isSocketOpen()) {
    cleanupWebRtc();
    return;
  }

  let type = SERVER_TYPES.CALL_HANGUP;
  if (state.call.phase === CALL_PHASE.OUTGOING) {
    type = SERVER_TYPES.CALL_CANCEL;
  } else if (state.call.phase === CALL_PHASE.INCOMING) {
    type = SERVER_TYPES.CALL_REJECT;
  }
  sendCurrentCallMessage(type, { reason: 'page_unload' });
  cleanupWebRtc();
});

setConnectionStatus('Disconnected', 'disconnected');
setMainControls();
renderUsers();
renderRooms();
renderRecent();
renderMessages();
