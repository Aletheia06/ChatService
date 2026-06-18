'use strict';

// Keep ICE configuration in one place so production TURN credentials can be
// added without touching the call state or signaling code.
const RTC_CONFIG = {
  iceServers: [
    { urls: 'stun:stun.l.google.com:19302' }
  ]
};

class WebRtcCall {
  constructor(options) {
    this.localVideo = options.localVideo;
    this.remoteVideo = options.remoteVideo;
    this.sendSignal = options.sendSignal;
    this.onStatus = options.onStatus;
    this.onFailure = options.onFailure;
    this.peerConnection = null;
    this.localStream = null;
    this.remoteStream = null;
    this.pendingIceCandidates = [];
    this.closing = false;
  }

  async prepareMedia() {
    if (this.localStream && this.peerConnection) {
      return;
    }

    this.closing = false;
    try {
      this.onStatus('Requesting camera and microphone...');
      const stream = await navigator.mediaDevices.getUserMedia({
        video: true,
        audio: true
      });
      if (this.closing) {
        for (const track of stream.getTracks()) {
          track.stop();
        }
        throw new Error('Call was cancelled.');
      }
      this.localStream = stream;
      this.localVideo.srcObject = this.localStream;
      this.createPeerConnection();
      for (const track of this.localStream.getTracks()) {
        this.peerConnection.addTrack(track, this.localStream);
      }
    } catch (error) {
      this.close();
      throw new Error(this.mediaErrorMessage(error));
    }
  }

  createPeerConnection() {
    if (this.peerConnection) {
      return;
    }

    const peerConnection = new RTCPeerConnection(RTC_CONFIG);
    this.peerConnection = peerConnection;

    peerConnection.addEventListener('icecandidate', (event) => {
      if (event.candidate) {
        this.sendSignal('ice_candidate', {
          candidate: event.candidate.toJSON()
        });
      }
    });

    peerConnection.addEventListener('track', (event) => {
      if (event.streams && event.streams[0]) {
        this.remoteStream = event.streams[0];
      } else {
        if (!this.remoteStream) {
          this.remoteStream = new MediaStream();
        }
        this.remoteStream.addTrack(event.track);
      }
      this.remoteVideo.srcObject = this.remoteStream;
      this.onStatus('Connected');
    });

    peerConnection.addEventListener('connectionstatechange', () => {
      if (this.closing || this.peerConnection !== peerConnection) {
        return;
      }

      const connectionState = peerConnection.connectionState;
      if (connectionState === 'connected') {
        this.onStatus('Connected');
      } else if (connectionState === 'connecting') {
        this.onStatus('Connecting media...');
      } else if (connectionState === 'disconnected') {
        this.onStatus('Media connection interrupted...');
      } else if (connectionState === 'failed') {
        this.onFailure('WebRTC media connection failed.');
      }
    });
  }

  async startAsCaller() {
    await this.prepareMedia();
    this.onStatus('Creating secure media connection...');
    const offer = await this.peerConnection.createOffer();
    await this.peerConnection.setLocalDescription(offer);
    this.sendSignal('webrtc_offer', {
      sdp: this.peerConnection.localDescription.toJSON()
    });
  }

  async receiveOffer(sdp) {
    await this.prepareMedia();
    await this.peerConnection.setRemoteDescription(sdp);
    await this.flushPendingIceCandidates();
    const answer = await this.peerConnection.createAnswer();
    await this.peerConnection.setLocalDescription(answer);
    this.sendSignal('webrtc_answer', {
      sdp: this.peerConnection.localDescription.toJSON()
    });
    this.onStatus('Connecting media...');
  }

  async receiveAnswer(sdp) {
    if (!this.peerConnection) {
      throw new Error('Received a WebRTC answer before the call was prepared.');
    }
    await this.peerConnection.setRemoteDescription(sdp);
    await this.flushPendingIceCandidates();
    this.onStatus('Connecting media...');
  }

  async addIceCandidate(candidate) {
    if (!candidate) {
      return;
    }
    if (!this.peerConnection || !this.peerConnection.remoteDescription) {
      this.pendingIceCandidates.push(candidate);
      return;
    }
    await this.peerConnection.addIceCandidate(candidate);
  }

  async flushPendingIceCandidates() {
    if (!this.peerConnection || !this.peerConnection.remoteDescription) {
      return;
    }
    const candidates = this.pendingIceCandidates.splice(0);
    for (const candidate of candidates) {
      await this.peerConnection.addIceCandidate(candidate);
    }
  }

  toggleMicrophone() {
    const track = this.localStream && this.localStream.getAudioTracks()[0];
    if (!track) {
      return false;
    }
    track.enabled = !track.enabled;
    return track.enabled;
  }

  toggleCamera() {
    const track = this.localStream && this.localStream.getVideoTracks()[0];
    if (!track) {
      return false;
    }
    track.enabled = !track.enabled;
    return track.enabled;
  }

  close() {
    this.closing = true;
    this.pendingIceCandidates = [];

    if (this.peerConnection) {
      this.peerConnection.close();
      this.peerConnection = null;
    }
    if (this.localStream) {
      for (const track of this.localStream.getTracks()) {
        track.stop();
      }
      this.localStream = null;
    }

    this.remoteStream = null;
    this.localVideo.srcObject = null;
    this.remoteVideo.srcObject = null;
  }

  mediaErrorMessage(error) {
    if (!window.isSecureContext && window.location.hostname !== 'localhost') {
      return 'Camera and microphone require HTTPS on public deployments.';
    }
    if (error && (error.name === 'NotAllowedError' || error.name === 'SecurityError')) {
      return 'Camera or microphone permission was denied.';
    }
    if (error && error.name === 'NotFoundError') {
      return 'No camera or microphone was found.';
    }
    return `Could not access camera or microphone: ${error && error.message ? error.message : 'unknown error'}`;
  }
}

window.ChatWebRTC = {
  RTC_CONFIG,
  WebRtcCall
};
