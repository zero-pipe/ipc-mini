    const $ = (id) => document.getElementById(id);
    const ui = {
      go: $('go'),
      goLabel: $('goLabel'),
      room: $('room'),
      wsUrl: $('wsUrl'),
      video: $('v'),
      canvas: $('c'),
      stage: $('stage'),
      empty: $('emptyState'),
      emptyTitle: $('emptyTitle'),
      globalState: $('globalState'),
      globalStateText: $('globalStateText'),
      liveBadge: $('liveBadge'),
      log: $('log'),
      talk: $('talk'),
      talkSide: $('talkSide'),
      muteSide: $('muteSide'),
      modeLive: $('modeLive'),
      modeReplay: $('modeReplay'),
      replayBar: $('replayBar'),
      replayDay: $('replayDay'),
      replayStatus: $('replayStatus'),
    };

    const iceServers = [
      { urls: 'stun:114.55.94.148:3478' },
      {
        urls: 'turn:114.55.94.148:3478?transport=udp',
        username: 'zeromini',
        credential: 'cqw881013',
      },
    ];

    let pc = null;
    let micStream = null;
    let talkEnabled = false;
    let audioMuted = false;
    let audioSender = null;
    let ws = null;
    let masterId = null;
    let dc = null;
    let offerStarted = false;
    let connectedIntent = false;
    let logRecords = [];
    let viewMode = 'live';
    let hlsPlayer = null;
    let replayToken = 0;
    let replayFillingDays = false;
    let pingTimer = null;
    let reconnectTimer = null;

    function formatValue(value) {
      if (value instanceof Error) return value.message;
      if (typeof value === 'object') {
        try { return JSON.stringify(value); } catch (_) { return String(value); }
      }
      return String(value);
    }

    function log(level, ...values) {
      const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
      const message = values.map(formatValue).join(' ');
      logRecords.push(`[${time}] [${level.toUpperCase()}] ${message}`);
      if (logRecords.length > 300) {
        logRecords = logRecords.slice(-300);
        ui.log.firstElementChild?.remove();
      }
      const line = document.createElement('div');
      line.className = `log-line ${level}`;
      line.innerHTML =
        `<span class="log-time">${time}</span>` +
        `<span class="log-level">${level.toUpperCase()}</span>` +
        `<span class="log-message"></span>`;
      line.lastElementChild.textContent = message;
      ui.log.appendChild(line);
      ui.log.scrollTop = ui.log.scrollHeight;
    }

    function setGlobalState(text, tone = 'idle') {
      ui.globalState.dataset.tone = tone;
      ui.globalStateText.textContent = text;
      ui.globalState.title = text;
    }

    function setStatus(name, text, tone = '') {
      $(`${name}State`).textContent = text;
      $(`${name}Dot`).className = `status-mark ${tone}`;
      $(`${name}Row`).dataset.tone = tone || 'idle';
      $(`${name}Row`).title = text;
    }

    const peerStateText = {
      new: '未建立',
      connecting: '连接中',
      connected: '在线',
      disconnected: '已断开',
      failed: '连接失败',
      closed: '已关闭',
    };

    const iceStateText = {
      new: '未建立',
      checking: '检测中',
      connected: '已连接',
      completed: '已完成',
      disconnected: '已断开',
      failed: '连接失败',
      closed: '已关闭',
    };

    function showEmpty(title) {
      ui.emptyTitle.textContent = title;
      ui.empty.classList.remove('hidden');
    }

    function updateButton() {
      ui.goLabel.textContent = connectedIntent ? '断开' : '观看';
      ui.go.classList.toggle('disconnect', connectedIntent);
      ui.go.setAttribute('aria-label', connectedIntent ? '断开连接' : '开始观看');
      ui.go.innerHTML = connectedIntent
        ? `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" aria-hidden="true"><path d="M8 8l8 8M16 8l-8 8"/><circle cx="12" cy="12" r="8.5"/></svg><span id="goLabel">断开</span>`
        : `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" aria-hidden="true"><circle cx="12" cy="12" r="8.5"/><path d="m10 8.8 6 3.2-6 3.2z" fill="currentColor" stroke="none"/></svg><span id="goLabel">观看</span>`;
      ui.goLabel = $('goLabel');
      ui.room.disabled = connectedIntent || viewMode === 'replay';
      ui.wsUrl.disabled = connectedIntent || viewMode === 'replay';
      if (viewMode === 'replay') {
        ui.goLabel.textContent = '刷新';
        ui.go.classList.remove('disconnect');
        ui.go.setAttribute('aria-label', '刷新回放列表');
        ui.go.innerHTML =
          `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" aria-hidden="true"><path d="M20 12a8 8 0 1 1-2.2-5.5"/><path d="M20 4v6h-6"/></svg><span id="goLabel">刷新</span>`;
        ui.goLabel = $('goLabel');
      }
    }

    function clearCanvas() {
      const ctx = ui.canvas.getContext('2d');
      ctx.clearRect(0, 0, ui.canvas.width, ui.canvas.height);
      $('metricObjects').textContent = '0';
    }

    const cocoNames = [
      '人', '自行车', '汽车', '摩托车', '飞机', '公交车', '火车', '卡车', '船', '红绿灯',
      '消防栓', '停车标志', '停车计时器', '长椅', '鸟', '猫', '狗', '马', '羊', '牛',
      '大象', '熊', '斑马', '长颈鹿', '背包', '伞', '手提包', '领带', '行李箱', '飞盘',
      '滑雪板', '单板', '球', '风筝', '球棒', '手套', '滑板', '冲浪板', '网球拍', '瓶子',
      '酒杯', '杯子', '叉子', '刀', '勺子', '碗', '香蕉', '苹果', '三明治', '橙子',
      '西兰花', '胡萝卜', '热狗', '披萨', '甜甜圈', '蛋糕', '椅子', '沙发', '盆栽', '床',
      '餐桌', '马桶', '电视', '笔记本', '鼠标', '遥控器', '键盘', '手机', '微波炉', '烤箱',
      '烤面包机', '水槽', '冰箱', '书', '钟', '花瓶', '剪刀', '玩具熊', '吹风机', '牙刷',
    ];

    function classLabel(cls) {
      const id = Number(cls);
      if (Number.isInteger(id) && id >= 0 && id < cocoNames.length) {
        return cocoNames[id];
      }
      return `类别${cls}`;
    }

    function drawBoxes(message) {
      const canvas = ui.canvas;
      const context = canvas.getContext('2d');
      const width = ui.video.videoWidth || 720;
      const height = ui.video.videoHeight || 480;
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }
      context.clearRect(0, 0, width, height);
      context.lineWidth = Math.max(2, width / 360);
      context.font = `600 ${Math.max(13, width / 55)}px sans-serif`;
      context.textBaseline = 'bottom';

      const objects = message.objs || [];
      $('metricObjects').textContent = String(objects.length);
      objects.forEach((object) => {
        const x = object.x * width;
        const y = object.y * height;
        const boxWidth = object.bw * width;
        const boxHeight = object.bh * height;
        const score = Math.round((Number(object.s) || 0) * 100);
        const label = `${classLabel(object.cls)} ${score}%`;
        const labelWidth = context.measureText(label).width + 12;
        const labelHeight = Math.max(21, width / 38);

        context.strokeStyle = '#42e8a7';
        context.fillStyle = 'rgba(6, 20, 30, .82)';
        context.strokeRect(x, y, boxWidth, boxHeight);
        context.fillRect(x, Math.max(0, y - labelHeight), labelWidth, labelHeight);
        context.fillStyle = '#caffea';
        context.fillText(label, x + 6, Math.max(labelHeight - 4, y - 4));
      });
    }

    function preferPcmu(transceiver) {
      if (!transceiver || typeof transceiver.setCodecPreferences !== 'function') {
        return;
      }
      const caps = RTCRtpSender.getCapabilities?.('audio');
      if (!caps?.codecs?.length) return;
      const pcmu = caps.codecs.filter(
        (codec) => String(codec.mimeType || '').toLowerCase() === 'audio/pcmu');
      if (pcmu.length) {
        try { transceiver.setCodecPreferences(pcmu); } catch (_) {}
      }
    }

    function setTalkUi(enabled) {
      talkEnabled = enabled;
      ui.talk.classList.toggle('active', enabled);
      ui.talkSide.classList.toggle('active', enabled);
      ui.talk.setAttribute('aria-label', enabled ? '结束对讲' : '对讲');
      ui.talk.dataset.tip = enabled ? '结束对讲' : '对讲';
      ui.talk.title = enabled ? '结束对讲' : '对讲';
      ui.talkSide.setAttribute('aria-label', enabled ? '结束对讲' : '对讲');
    }

    function setMuteUi(muted) {
      audioMuted = muted;
      ui.muteSide.classList.toggle('active', muted);
      ui.muteSide.setAttribute('aria-label', muted ? '取消静音' : '静音');
      ui.video.muted = muted;
    }

    async function enableTalk(enable) {
      if (!pc || !audioSender) {
        log('warn', '对讲尚未就绪');
        return;
      }
      if (!enable) {
        const track = audioSender.track;
        if (track) track.enabled = false;
        setTalkUi(false);
        log('info', '对讲已关闭');
        return;
      }
      try {
        if (!micStream) {
          micStream = await navigator.mediaDevices.getUserMedia({
            audio: {
              channelCount: 1,
              echoCancellation: true,
              noiseSuppression: true,
              autoGainControl: true,
            },
            video: false,
          });
        }
        const micTrack = micStream.getAudioTracks()[0];
        if (!micTrack) throw new Error('no microphone track');
        micTrack.enabled = true;
        await audioSender.replaceTrack(micTrack);
        setTalkUi(true);
        setMuteUi(false);
        await ui.video.play().catch(() => {});
        log('success', '对讲已开启');
      } catch (error) {
        setTalkUi(false);
        log('error', '打开麦克风失败：', error);
      }
    }

    function requestFullscreen() {
      const target = ui.stage;
      const request = target.requestFullscreen || target.webkitRequestFullscreen;
      if (request) {
        const result = request.call(target);
        result?.catch?.((error) => log('warn', '无法进入全屏：', error));
      }
    }

    function closePeer() {
      offerStarted = false;
      masterId = null;
      setTalkUi(false);
      ui.talk.disabled = true;
      ui.talkSide.disabled = true;
      audioSender = null;
      if (micStream) {
        micStream.getTracks().forEach((track) => track.stop());
        micStream = null;
      }
      if (dc) {
        try { dc.close(); } catch (_) {}
      }
      dc = null;
      if (pc) {
        pc.ontrack = null;
        pc.onconnectionstatechange = null;
        pc.oniceconnectionstatechange = null;
        try { pc.close(); } catch (_) {}
      }
      pc = null;
      ui.video.srcObject = null;
      setMuteUi(false);
      ui.liveBadge.classList.remove('visible');
      $('metricVideo').textContent = '未接收';
      $('metricSize').textContent = '—';
      $('metricData').textContent = '未连接';
      clearCanvas();
      setStatus('device', '等待连接', 'working');
      setStatus('ice', '未建立');
      setStatus('ai', '未连接');
    }

    function clearLiveTimers() {
      if (pingTimer) {
        clearInterval(pingTimer);
        pingTimer = null;
      }
      if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
      }
    }

    function startSignalingPing() {
      if (pingTimer) {
        clearInterval(pingTimer);
      }
      pingTimer = setInterval(() => {
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ type: 'ping' }));
        }
      }, 20000);
    }

    function disconnect(reason = '用户主动断开') {
      connectedIntent = false;
      clearLiveTimers();
      if (ws) {
        ws.onclose = null;
        try { ws.close(); } catch (_) {}
      }
      ws = null;
      closePeer();
      updateButton();
      setStatus('ws', '未连接');
      setGlobalState('未连接');
      showEmpty('等待连接');
      log('info', reason);
    }

    async function startPeer() {
      if (offerStarted || !masterId || !ws || ws.readyState !== WebSocket.OPEN) return;
      offerStarted = true;
      setGlobalState('连接中', 'working');
      setStatus('device', '协商中', 'working');
      log('info', '发现设备，开始 WebRTC 协商');

      try {
        pc = new RTCPeerConnection({ iceServers });
        pc.onconnectionstatechange = () => {
          if (!pc) return;
          const state = pc.connectionState;
          const tone = state === 'connected'
            ? 'online'
            : ['disconnected', 'failed', 'closed'].includes(state) ? 'error' : 'working';
          setStatus('device', peerStateText[state] || '未知状态', tone);
          log(state === 'connected' ? 'success' : 'info', 'PeerConnection：', state);
          if (state === 'connected') {
            setGlobalState('在线', 'online');
            if (!audioMuted) {
              ui.video.muted = false;
              ui.video.play().catch(() => {});
            }
          } else if (state === 'failed') {
            setGlobalState('失败', 'error');
            showEmpty('连接失败');
          }
        };
        pc.oniceconnectionstatechange = () => {
          if (!pc) return;
          const state = pc.iceConnectionState;
          const tone = ['connected', 'completed'].includes(state)
            ? 'online'
            : ['disconnected', 'failed', 'closed'].includes(state) ? 'error' : 'working';
          setStatus('ice', iceStateText[state] || '未知状态', tone);
          log(tone === 'error' ? 'error' : 'info', 'ICE：', state);
        };
        pc.ontrack = (event) => {
          let stream = ui.video.srcObject;
          if (!(stream instanceof MediaStream)) {
            stream = event.streams[0] || new MediaStream();
            ui.video.srcObject = stream;
          }
          if (!stream.getTracks().includes(event.track)) {
            stream.addTrack(event.track);
          }
          ui.video.play().catch((error) => log('warn', '浏览器阻止自动播放：', error));
          if (event.track.kind === 'audio') {
            log('success', '已接收音频轨道');
          } else {
            $('metricVideo').textContent = '接收中';
            ui.empty.classList.add('hidden');
            ui.liveBadge.classList.add('visible');
            log('success', '已接收视频轨道');
          }
        };
        pc.onicecandidate = (event) => {
          if (!event.candidate || !ws || ws.readyState !== WebSocket.OPEN) return;
          ws.send(JSON.stringify({
            type: 'candidate',
            candidate: event.candidate.candidate,
            sdpMid: event.candidate.sdpMid,
            sdpMLineIndex: event.candidate.sdpMLineIndex,
            to: masterId,
          }));
        };

        dc = pc.createDataChannel('detect', { ordered: true });
        dc.onopen = () => {
          $('metricData').textContent = '已连接';
          setStatus('ai', '等待数据', 'working');
          log('success', 'AI 数据通道已连接');
        };
        dc.onclose = () => {
          $('metricData').textContent = '已关闭';
          setStatus('ai', '已断开');
          log('warn', 'AI 数据通道已关闭');
        };
        dc.onerror = (event) => log('error', 'AI 数据通道异常：', event?.message || 'unknown');
        dc.onmessage = (event) => {
          try {
            const text = typeof event.data === 'string'
              ? event.data
              : new TextDecoder().decode(event.data);
            const message = JSON.parse(text);
            drawBoxes(message);
            setStatus('ai', '运行中', 'online');
            if (!dc._loggedFirst) {
              dc._loggedFirst = true;
              log('success', '收到首帧检测结果');
            }
          } catch (error) {
            log('error', '解析检测结果失败：', error);
          }
        };

        pc.addTransceiver('video', { direction: 'recvonly' });
        const audioXcvr = pc.addTransceiver('audio', { direction: 'sendrecv' });
        preferPcmu(audioXcvr);
        audioSender = audioXcvr.sender;
        try {
          const silence = await navigator.mediaDevices.getUserMedia({
            audio: { channelCount: 1, echoCancellation: true },
            video: false,
          });
          micStream = silence;
          const track = silence.getAudioTracks()[0];
          if (track) {
            track.enabled = false;
            await audioSender.replaceTrack(track);
          }
        } catch (error) {
          log('warn', '预申请麦克风失败：', error);
        }

        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        ws.send(JSON.stringify({ type: 'offer', sdp: offer.sdp, to: masterId }));
        ui.talk.disabled = false;
        ui.talkSide.disabled = false;
        log('info', 'Offer 已发送，等待设备应答');
      } catch (error) {
        offerStarted = false;
        setGlobalState('协商失败', 'error');
        log('error', '创建 WebRTC 连接失败：', error);
      }
    }

    function connect() {
      const url = ui.wsUrl.value.trim();
      const room = ui.room.value.trim();
      if (!url || !room) {
        log('warn', '请填写信令服务器和设备房间');
        return;
      }

      clearLiveTimers();
      closePeer();
      connectedIntent = true;
      updateButton();
      setGlobalState('连接中', 'working');
      setStatus('ws', '连接中', 'working');
      showEmpty('连接中…');
      log('info', '连接房间：', room);

      try {
        ws = new WebSocket(url);
      } catch (error) {
        log('error', '无效的信令地址：', error);
        disconnect('连接未建立');
        return;
      }

      ws.onopen = () => {
        setStatus('ws', '已连接', 'online');
        setGlobalState('查找设备', 'working');
        log('success', '信令服务已连接');
        startSignalingPing();
        ws.send(JSON.stringify({ type: 'join', role: 'viewer', room }));
      };

      ws.onmessage = async (event) => {
        let message;
        try {
          message = JSON.parse(event.data);
        } catch (error) {
          log('error', '收到无效信令消息：', error);
          return;
        }

        if (message.type === 'joined') {
          if (message.masterId) {
            masterId = message.masterId;
            await startPeer();
          } else {
            setStatus('device', '离线', 'error');
            setGlobalState('设备离线', 'error');
            showEmpty('设备离线');
            log('warn', '设备当前不在线，正在等待');
          }
          return;
        }
        if (message.type === 'peer-joined' && message.role === 'master') {
          masterId = message.clientId;
          log('success', '设备已上线');
          await startPeer();
          return;
        }
        if (message.type === 'peer-left' && message.role === 'master') {
          closePeer();
          setGlobalState('设备离线', 'error');
          showEmpty('设备已断开');
          log('warn', '设备已离线');
          return;
        }
        if (message.type === 'answer' && pc) {
          try {
            await pc.setRemoteDescription({ type: 'answer', sdp: message.sdp });
            log('success', '设备应答已生效');
          } catch (error) {
            log('error', '应用设备应答失败：', error);
          }
          return;
        }
        if (message.type === 'candidate' && pc && message.candidate) {
          const candidate = {
            candidate: message.candidate,
            sdpMid: message.sdpMid || '0',
            sdpMLineIndex: message.sdpMLineIndex ?? 0,
          };
          try {
            await pc.addIceCandidate(candidate);
          } catch (error) {
            log('error', '添加 ICE Candidate 失败：', error);
          }
          return;
        }
        if (message.type === 'pong') {
          return;
        }
        if (message.type === 'error') {
          const detail = message.message || '信令服务返回错误';
          setGlobalState('被拒绝', 'error');
          showEmpty('无法加入');
          log('error', detail);
        }
      };

      ws.onclose = () => {
        if (!connectedIntent || viewMode !== 'live') {
          return;
        }
        ws = null;
        closePeer();
        clearLiveTimers();
        setStatus('ws', '已断开', 'error');
        setGlobalState('重连中', 'working');
        showEmpty('信令断开，正在重连…');
        log('warn', '信令连接已关闭，1.5 秒后重连');
        reconnectTimer = setTimeout(() => {
          reconnectTimer = null;
          if (connectedIntent && viewMode === 'live') {
            connect();
          }
        }, 1500);
      };
      ws.onerror = () => {
        setStatus('ws', '异常', 'error');
        setGlobalState('异常', 'error');
        log('error', '信令服务连接异常');
      };
    }

    ui.video.addEventListener('loadedmetadata', () => {
      const width = ui.video.videoWidth;
      const height = ui.video.videoHeight;
      if (width && height) {
        ui.stage.style.aspectRatio = `${width} / ${height}`;
        ui.canvas.width = width;
        ui.canvas.height = height;
        $('metricSize').textContent = `${width}×${height}`;
      }
    });

    function onTalkClick() {
      enableTalk(!talkEnabled);
    }

    ui.talk.addEventListener('click', onTalkClick);
    ui.talkSide.addEventListener('click', onTalkClick);

    function recordBaseUrl() {
      const room = encodeURIComponent(ui.room.value.trim() || 'door-1');
      return `${location.origin}/record/${room}`;
    }

    function setReplayStatus(text) {
      ui.replayStatus.textContent = text;
    }

    function stopReplay() {
      replayToken += 1;
      if (hlsPlayer) {
        try { hlsPlayer.destroy(); } catch (_) {}
        hlsPlayer = null;
      }
      ui.video.pause();
      ui.video.removeAttribute('src');
      ui.video.srcObject = null;
    }

    function loadScript(src) {
      return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.src = src;
        script.onload = () => resolve();
        script.onerror = () => reject(new Error(src));
        document.head.appendChild(script);
      });
    }

    async function ensureHls() {
      if (window.Hls) {
        return;
      }
      const sources = [
        'https://cdn.jsdelivr.net/npm/hls.js@1.5.20/dist/hls.min.js',
        'https://cdn.bootcdn.net/ajax/libs/hls.js/1.5.20/hls.min.js',
      ];
      for (const src of sources) {
        try {
          await loadScript(src);
          if (window.Hls) {
            return;
          }
        } catch (_) {}
      }
    }

    async function playPlaylist(url) {
      const token = ++replayToken;
      if (hlsPlayer) {
        try { hlsPlayer.destroy(); } catch (_) {}
        hlsPlayer = null;
      }
      ui.video.pause();
      ui.video.removeAttribute('src');
      ui.video.srcObject = null;
      ui.liveBadge.classList.remove('visible');
      $('metricVideo').textContent = '回放中';
      $('metricData').textContent = '录像';
      setReplayStatus('正在加载…');
      showEmpty('正在加载录像…');
      log('info', '打开播放列表', url);

      let segmentCount = 0;
      try {
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const text = await response.text();
        segmentCount = text.split(/\r?\n/).filter((line) =>
          /\.m4s$/i.test(line.trim())).length;
        log('info', '播放列表切片', segmentCount);
        if (segmentCount === 0) {
          setReplayStatus('还没有切片，等第一段关完再刷新');
          showEmpty('还没有可播切片');
          return;
        }
      } catch (error) {
        setReplayStatus('无法读取播放列表');
        showEmpty('无法读取播放列表');
        log('error', '读取播放列表失败：', error);
        return;
      }
      if (token !== replayToken || viewMode !== 'replay') {
        return;
      }

      await ensureHls();
      if (token !== replayToken || viewMode !== 'replay') {
        return;
      }

      const startPlayback = () => {
        if (token !== replayToken) {
          return;
        }
        setMuteUi(true);
        ui.video.play().then(() => {
          if (token !== replayToken) {
            return;
          }
          ui.empty.classList.add('hidden');
          setReplayStatus(`播放中 · ${segmentCount} 段`);
          setGlobalState('回放', 'online');
          log('success', '回放已开始');
        }).catch((error) => {
          if (token !== replayToken) {
            return;
          }
          log('warn', '回放自动播放被拦截：', error);
          setReplayStatus('点画面开始播放');
          showEmpty('点画面开始播放');
        });
      };

      const canNative = ui.video.canPlayType('application/vnd.apple.mpegurl');
      if (window.Hls?.isSupported()) {
        hlsPlayer = new Hls({
          enableWorker: true,
          lowLatencyMode: false,
          startPosition: 0,
          liveSyncDurationCount: 1,
          liveMaxLatencyDurationCount: 8,
          maxBufferLength: 30,
          maxMaxBufferLength: 90,
          manifestLoadingTimeOut: 15000,
          fragLoadingTimeOut: 60000,
        });
        hlsPlayer.on(Hls.Events.MANIFEST_PARSED, () => {
          setReplayStatus(`已加载 ${segmentCount} 段，正在缓冲…`);
          startPlayback();
        });
        hlsPlayer.on(Hls.Events.FRAG_BUFFERED, () => {
          if (token !== replayToken) {
            return;
          }
          ui.empty.classList.add('hidden');
        });
        hlsPlayer.on(Hls.Events.ERROR, (_, data) => {
          if (token !== replayToken || !data?.fatal) {
            return;
          }
          setReplayStatus('回放失败');
          showEmpty('回放失败');
          log('error', 'HLS 回放失败：', data.type, data.details);
        });
        hlsPlayer.loadSource(url);
        hlsPlayer.attachMedia(ui.video);
        return;
      }
      if (canNative) {
        ui.video.src = url;
        startPlayback();
        setReplayStatus('浏览器原生播放');
        return;
      }
      setReplayStatus('当前浏览器不支持 HLS');
      showEmpty('当前浏览器不支持回放');
      log('error', '当前浏览器不支持 MSE HLS，也无法原生播放 m3u8');
    }

    async function loadReplayDays() {
      const url = recordBaseUrl();
      setReplayStatus('正在列出录像…');
      try {
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const body = await response.json();
        const days = (body.files || [])
          .filter((name) => /^\d{4}-\d{2}-\d{2}$/.test(name))
          .sort()
          .reverse();
        replayFillingDays = true;
        ui.replayDay.replaceChildren();
        if (!days.length) {
          replayFillingDays = false;
          setReplayStatus('还没有可回放的录像');
          showEmpty('暂无录像');
          log('warn', '云端还没有录像目录');
          return;
        }
        for (const day of days) {
          const option = document.createElement('option');
          option.value = day;
          option.textContent = day;
          ui.replayDay.appendChild(option);
        }
        const selected = days[0];
        ui.replayDay.value = selected;
        replayFillingDays = false;
        log('info', '回放日期：', days.join(', '));
        playPlaylist(`${url}/${selected}/index.m3u8`);
      } catch (error) {
        setReplayStatus('无法列出录像');
        showEmpty('无法读取录像');
        log('error', '列出录像失败：', error);
      }
    }

    function setViewMode(mode) {
      if (mode === viewMode) {
        return;
      }
      viewMode = mode;
      const replay = mode === 'replay';
      ui.modeLive.classList.toggle('active', !replay);
      ui.modeReplay.classList.toggle('active', replay);
      ui.modeLive.setAttribute('aria-selected', String(!replay));
      ui.modeReplay.setAttribute('aria-selected', String(replay));
      ui.replayBar.classList.toggle('hidden', !replay);
      ui.talk.hidden = replay;
      ui.talkSide.hidden = replay;
      ui.talkSide.disabled = replay || !pc;
      if (replay) {
        if (connectedIntent) {
          disconnect('切换到回放');
        }
        ui.canvas.style.display = 'none';
        clearCanvas();
        setGlobalState('回放', 'working');
        showEmpty('正在加载录像…');
        updateButton();
        loadReplayDays();
      } else {
        stopReplay();
        ui.canvas.style.display = '';
        ui.video.srcObject = null;
        $('metricVideo').textContent = '未接收';
        $('metricData').textContent = '未连接';
        setReplayStatus('选择日期后播放');
        setGlobalState('未连接');
        showEmpty('等待连接');
        updateButton();
      }
    }

    ui.go.addEventListener('click', () => {
      if (viewMode === 'replay') {
        loadReplayDays();
        return;
      }
      if (connectedIntent) disconnect();
      else connect();
    });

    ui.modeLive.addEventListener('click', () => setViewMode('live'));
    ui.modeReplay.addEventListener('click', () => setViewMode('replay'));
    ui.replayDay.addEventListener('change', () => {
      if (replayFillingDays) {
        return;
      }
      const day = ui.replayDay.value;
      if (!day) return;
      playPlaylist(`${recordBaseUrl()}/${day}/index.m3u8`);
    });

    ui.video.addEventListener('click', () => {
      if (viewMode !== 'replay') {
        return;
      }
      ui.video.play().catch(() => {});
    });

    $('fullscreen').addEventListener('click', requestFullscreen);
    $('fullscreenSide').addEventListener('click', requestFullscreen);

    ui.muteSide.addEventListener('click', () => {
      setMuteUi(!audioMuted);
      log('info', audioMuted ? '已静音' : '已取消静音');
    });

    $('clearLog').addEventListener('click', () => {
      logRecords = [];
      ui.log.textContent = '';
    });

    $('copyLog').addEventListener('click', async () => {
      try {
        await navigator.clipboard.writeText(logRecords.join('\n'));
        log('success', '日志已复制');
      } catch (error) {
        log('warn', '复制失败：', error);
      }
    });

    window.addEventListener('beforeunload', () => {
      clearLiveTimers();
      try { ws?.close(); } catch (_) {}
      try { pc?.close(); } catch (_) {}
      stopReplay();
    });

    updateButton();
    log('info', '页面已就绪');
    if (location.hash === '#replay') {
      setViewMode('replay');
    }
