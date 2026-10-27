export function startScope(canvas, getAnalyser, getNode) {
  const ctx2 = canvas.getContext("2d");
  const buf = new Float32Array(2048);
  function draw() {
    requestAnimationFrame(draw);
    const dpr = devicePixelRatio || 1;
    const w = Math.round(canvas.clientWidth * dpr);
    const h = Math.round(canvas.clientHeight * dpr);
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    const css = getComputedStyle(document.documentElement);
    ctx2.clearRect(0, 0, w, h);
    ctx2.strokeStyle = css.getPropertyValue("--line");
    ctx2.lineWidth = 1;
    ctx2.beginPath();
    ctx2.moveTo(0, h / 2);
    ctx2.lineTo(w, h / 2);
    ctx2.stroke();
    ctx2.strokeStyle = css.getPropertyValue("--acc").trim();
    ctx2.lineWidth = 1.5 * dpr;
    ctx2.beginPath();
    const analyser = getAnalyser();
    const node = getNode();
    if (!analyser || !node) {
      ctx2.moveTo(0, h / 2);
      ctx2.lineTo(w, h / 2);
      ctx2.stroke();
      return;
    }
    analyser.getFloatTimeDomainData(buf);
    let start = 1;
    while (start < 1024 && !(buf[start - 1] < 0 && buf[start] >= 0)) start++;
    if (start >= 1024) start = 0;
    const N = 1024;
    for (let i = 0; i < N; i++) {
      const x = (i / (N - 1)) * w;
      const y = h / 2 - Math.max(-1, Math.min(1, buf[start + i])) * (h / 2 - 2);
      if (i === 0) ctx2.moveTo(x, y);
      else ctx2.lineTo(x, y);
    }
    ctx2.stroke();
  }
  draw();
}
