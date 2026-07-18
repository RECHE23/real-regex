/* curve.js -- the guarantee panel: real (O(n), flat) vs a backtracking engine
 * (O(2^n), a cliff), on a canvas. Adapted from the approved landing mockup
 * (~/Projects/.recovery/real-regex-landing-mockup.html), vanilla JS, no
 * dependencies. Reduced-motion-safe: draws the settled frame immediately and
 * skips the animation loop under prefers-reduced-motion.
 *
 * The mockup wired its redraw to a page-local theme-toggle button (#themeBtn)
 * that does not exist on a pydata-sphinx-theme page -- pydata owns its own
 * light/dark switcher and stamps data-theme on <html>. This version observes
 * that attribute directly (MutationObserver) so the curve re-tints on every
 * theme change regardless of which control triggered it.
 */
(function () {
  "use strict";

  var cv = document.getElementById("curve");
  if (!cv) return; // only present on the landing page

  var root = document.documentElement;
  var reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  function css(v) {
    return getComputedStyle(root).getPropertyValue(v).trim();
  }

  function draw(progress) {
    var p = progress == null ? 1 : progress;
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var w = cv.clientWidth, h = cv.clientHeight;
    if (w === 0 || h === 0) return;
    cv.width = w * dpr;
    cv.height = h * dpr;
    var ctx = cv.getContext("2d");
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    var padL = 34, padB = 26, padT = 14, padR = 14;
    var x0 = padL, x1 = w - padR, y0 = h - padB, y1 = padT;
    var accent = css("--accent"), danger = css("--danger"), line = css("--line-2"), ink3 = css("--ink-3");

    ctx.strokeStyle = line;
    ctx.lineWidth = 1;
    for (var i = 0; i <= 4; i++) {
      var gy = y0 + (y1 - y0) * (i / 4);
      ctx.beginPath();
      ctx.moveTo(x0, gy);
      ctx.lineTo(x1, gy);
      ctx.stroke();
    }
    ctx.fillStyle = ink3;
    ctx.font = "10px ui-monospace, monospace";
    ctx.fillText("time", x0 - 30, y1 + 8);
    ctx.fillText("input length →", x1 - 92, y0 + 18);

    var span = x1 - x0;

    // real: flat linear (gentle slope, stays low)
    ctx.strokeStyle = accent;
    ctx.lineWidth = 2.5;
    ctx.lineJoin = "round";
    ctx.beginPath();
    var endA = x0 + span * p;
    for (var x = x0; x <= endA; x += 3) {
      var tt = (x - x0) / span;
      var yy = y0 - (y0 - y1) * (0.10 + 0.14 * tt);
      if (x === x0) ctx.moveTo(x, yy); else ctx.lineTo(x, yy);
    }
    ctx.stroke();
    var yA = y0 - (y0 - y1) * (0.10 + 0.14 * p);
    ctx.fillStyle = accent;
    ctx.beginPath();
    ctx.arc(endA, yA, 3.4, 0, 7);
    ctx.fill();

    // backtracking: exponential cliff
    ctx.strokeStyle = danger;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    var endB = x0 + span * 0.82 * p;
    for (var x2 = x0; x2 <= endB; x2 += 3) {
      var t2 = (x2 - x0) / (span * 0.82);
      var yv = Math.pow(t2, 3.2);
      var yy2 = y0 - (y0 - y1) * Math.min(yv, 1.02);
      if (x2 === x0) ctx.moveTo(x2, yy2); else ctx.lineTo(x2, yy2);
    }
    ctx.stroke();
    var tEnd = (endB - x0) / (span * 0.82);
    var yB = y0 - (y0 - y1) * Math.min(Math.pow(tEnd, 3.2), 1.02);
    ctx.fillStyle = danger;
    ctx.beginPath();
    ctx.arc(endB, yB, 3.4, 0, 7);
    ctx.fill();
  }

  function run() {
    if (reduce) {
      draw(1);
      return;
    }
    var start = null;
    function step(ts) {
      if (start == null) start = ts;
      var p = Math.min((ts - start) / 1100, 1);
      var eased = 1 - Math.pow(1 - p, 3);
      draw(eased);
      if (p < 1) requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
  }

  var seen = false;
  if ("IntersectionObserver" in window) {
    var io = new IntersectionObserver(
      function (es) {
        es.forEach(function (en) {
          if (en.isIntersecting && !seen) {
            seen = true;
            run();
          }
        });
      },
      { threshold: 0.4 }
    );
    io.observe(cv);
  } else {
    run();
  }

  // Re-tint (not re-animate) on theme change, wherever the toggle lives.
  if ("MutationObserver" in window) {
    var mo = new MutationObserver(function () {
      draw(1);
    });
    mo.observe(root, { attributes: true, attributeFilter: ["data-theme"] });
  }

  var rt;
  window.addEventListener("resize", function () {
    clearTimeout(rt);
    rt = setTimeout(function () {
      draw(1);
    }, 120);
  });

  draw(reduce ? 1 : 0.001);
})();
