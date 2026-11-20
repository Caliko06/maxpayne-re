// The way into the game: the picture of what you picked tears in two over black. One pass, one texture, gone with the launcher.
// PaperTear.glsl by gre, MIT, from github.com/gl-transitions/gl-transitions (transitions/PaperTear.glsl). Its uniforms are constants here.
'use strict';
(() => {
  const FRAG = `precision highp float;
uniform sampler2D from; uniform vec2 fit; uniform float progress, ratio;
varying vec2 v;
vec4 getFromColor(vec2 uv) { return texture2D(from, vec2(.5) + (vec2(uv.x, 1. - uv.y) - .5) * fit); }
vec4 getToColor(vec2 uv) { return vec4(0., 0., 0., 1.); }
const float roughness = 0.12;
const float tilt = 0.1;
const float rim = 0.012;
const vec4 paper = vec4(0.914, 0.894, 0.847, 1.0);  // the launcher's paper, was vec4(1.0, 0.98, 0.94, 1.0)
const vec4 shadow = vec4(0.0, 0.0, 0.0, 0.35);

float tri (float x) {
  return abs(fract(x) - 0.5) - 0.25;
}

// horizontal position of the tear at height y: smooth meanders...
float meander (float y) {
  return 0.5 * ratio + roughness * (0.5 * sin(y * 4.7 + 1.3) + 0.25 * sin(y * 11.3 + 4.1));
}

// ...and sharp jags
float tear (float y) {
  return meander(y) + roughness * (0.1 * sin(y * 29.0 + 0.7) + 0.35 * tri(y * 19.0 + sin(y * 7.0)) + 0.12 * tri(y * 47.0 + 0.6 * sin(y * 13.0)));
}

// width of the torn edge along it, so that the fibers look uneven
float fibers (float y, float seed) {
  return rim * (0.7 + 0.3 * sin(y * 37.0 + seed) + 0.4 * tri(y * 83.0 + seed));
}

// the left half (or the mirrored right half) slides out, its top leaning away first:
// from a point on screen to the point of the paper (the frame edges stay covered)
vec2 unfold (vec2 q, float away, float lean) {
  return vec2(q.x + away + lean * q.y, q.y);
}

// a torn half over the background, with its fibers and an anti-aliased edge
// e: distance past the torn edge, limit: how far the fibers reach, aa: edge softness
vec4 torn (vec4 bg, vec4 col, float e, float limit, float aa) {
  vec4 c = vec4(mix(col.rgb, paper.rgb, clamp(0.5 + e / aa, 0.0, 1.0) * paper.a), col.a);
  return mix(bg, c, clamp(0.5 - (e - limit) / aa, 0.0, 1.0));
}

vec4 transition (vec2 uv) {
  float p = progress;
  float away = p * p * (0.5 * ratio + 0.1 + roughness + 2.0 * rim); // far enough to leave the frame
  float lean = tilt * p * (2.0 - p);
  float fiber = smoothstep(0.0, 0.15, p); // no torn edge on the intact image
  float aa = 0.002 * fiber + 1e-6;
  vec2 q = vec2(uv.x * ratio, uv.y);
  vec2 l = unfold(q, away, lean);
  vec2 r = unfold(vec2(ratio - q.x, q.y), away, lean);
  float el = l.x - tear(l.y); // > 0 right of the left half's torn edge
  float er = r.x - (ratio - tear(r.y)); // > 0 left of the right half's torn edge
  // soft shadow of the two halves on the incoming image, cast toward the right
  vec2 sq = q - vec2(0.01, 0.0);
  float dl = unfold(sq, away, lean).x - meander(sq.y);
  float dr = unfold(vec2(ratio - sq.x, sq.y), away, lean).x - (ratio - meander(sq.y));
  vec4 to = getToColor(uv);
  float sh = shadow.a * (1.0 - smoothstep(-0.02, 0.08, min(dl, dr)));
  vec4 c = vec4(mix(to.rgb, shadow.rgb, sh), to.a);
  // the image stops at the tear; the bare paper fibers stick out a little beyond it
  if (r.x >= 0.0) {
    c = torn(c, getFromColor(vec2(1.0 - r.x / ratio, r.y)), er, fibers(r.y, 2.0) * 1.5 * fiber, aa);
  }
  if (l.x >= 0.0) {
    c = torn(c, getFromColor(vec2(l.x / ratio, l.y)), el, fibers(l.y, 0.0) * fiber, aa);
  }
  return c;
}
void main() { gl_FragColor = transition(v); }`;
  // Over `parent`: the image covers the screen in 180 ms, then tears for the rest of `ms`. Returns false if it cannot run (the caller's fade stays).
  function run(parent, img, ms) {
    const cv = document.createElement('canvas'), w = parent.clientWidth, h = parent.clientHeight;
    cv.width = w; cv.height = h; cv.style.cssText = 'position:absolute;inset:0;width:100%;height:100%;z-index:5;pointer-events:none';
    const gl = cv.getContext('webgl', { alpha: false, antialias: false }); if (!gl) return false;
    const sh = (t, s) => { const o = gl.createShader(t); gl.shaderSource(o, s); gl.compileShader(o); return o; };
    const p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, 'attribute vec2 a; varying vec2 v; void main() { v = a * .5 + .5; gl_Position = vec4(a, 0., 1.); }'));
    gl.attachShader(p, sh(gl.FRAGMENT_SHADER, FRAG)); gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) return false;
    gl.useProgram(p);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
    const at = gl.getAttribLocation(p, 'a'); gl.enableVertexAttribArray(at); gl.vertexAttribPointer(at, 2, gl.FLOAT, false, 0, 0);
    gl.bindTexture(gl.TEXTURE_2D, gl.createTexture());
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
    for (const [k, v] of [[gl.TEXTURE_MIN_FILTER, gl.LINEAR], [gl.TEXTURE_MAG_FILTER, gl.LINEAR], [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]]) gl.texParameteri(gl.TEXTURE_2D, k, v);
    const ar = w / h, ir = img.naturalWidth / img.naturalHeight;     // cover, not stretch
    gl.uniform2f(gl.getUniformLocation(p, 'fit'), Math.min(1, ar / ir), Math.min(1, ir / ar));
    gl.uniform1f(gl.getUniformLocation(p, 'ratio'), ar);
    const prog = gl.getUniformLocation(p, 'progress'), HOLD = 180;
    const draw = v => { gl.uniform1f(prog, v); gl.drawArrays(gl.TRIANGLES, 0, 3); };
    draw(0); parent.append(cv);
    cv.animate([{ opacity: 0 }, { opacity: 1 }], { duration: HOLD, easing: 'ease-out' });
    const t0 = performance.now();
    const tick = () => {
      const t = (performance.now() - t0 - HOLD) / (ms - HOLD);
      draw(Math.max(0, Math.min(1, t)));
      if (t < 1 && cv.isConnected) requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    return true;
  }
  window.MPTear = { run };
})();
