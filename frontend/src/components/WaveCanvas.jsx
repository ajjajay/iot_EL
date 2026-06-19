import { useEffect, useRef } from 'react';

export default function WaveCanvas() {
  const ref = useRef(null);

  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    let w = 0, h = 0, raf = 0;
    const mouse = { x: -9999, y: -9999 };

    const resize = () => {
      w = canvas.width = window.innerWidth;
      h = canvas.height = window.innerHeight;
    };
    resize();
    window.addEventListener('resize', resize);
    window.addEventListener('mousemove', e => { mouse.x = e.clientX; mouse.y = e.clientY; });

    let t = 0;
    const draw = () => {
      t += 0.004;
      ctx.clearRect(0, 0, w, h);

      const dark = document.body.dataset.theme === 'dark';
      const rgb = dark ? '139,92,246' : '109,40,217';

      for (let i = 0; i < 6; i++) {
        const alpha = Math.max(0, dark ? 0.035 - i * 0.005 : 0.022 - i * 0.003);
        ctx.beginPath();
        ctx.strokeStyle = `rgba(${rgb},${alpha})`;
        ctx.lineWidth = 1.5;

        for (let x = 0; x <= w; x += 5) {
          const baseY = h * (0.2 + i * 0.12);
          const naturalY = baseY
            + Math.sin(x * 0.004 + t + i * 0.9) * (14 + i * 6)
            + Math.sin(x * 0.010 - t * 1.2 + i * 0.5) * (6 + i * 2)
            + Math.cos(x * 0.003 + t * 0.5) * 4;

          // Push wave away from cursor when nearby
          const dx = x - mouse.x;
          const dy = naturalY - mouse.y;
          const dist = Math.sqrt(dx * dx + dy * dy);
          const proximity = 160;
          const push = dist < proximity
            ? (proximity - dist) / proximity * 28 * Math.sign(dy || 1)
            : 0;

          const y = naturalY + push;
          x === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
        ctx.stroke();
      }

      raf = requestAnimationFrame(draw);
    };
    draw();

    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', resize);
    };
  }, []);

  return (
    <canvas
      ref={ref}
      aria-hidden="true"
      style={{
        position: 'fixed', inset: 0,
        width: '100vw', height: '100vh',
        pointerEvents: 'none', zIndex: 1,
      }}
    />
  );
}
