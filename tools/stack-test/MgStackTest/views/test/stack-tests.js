// Stack stress tests for Magelight's page stack (1.3.0, "ulFiberMB"). Each test drives a page into runaway
// recursion by a different road, in its scripts or in the engine's layout, and reports what stopped it: a script
// gets "RangeError: Maximum call stack size exceeded" and the page goes on. A run that ends the overlay instead
// ("SEH exception 0xC00000FD ... overlay disabled" in Magelight.log) is the failure the page stack prevents.
// Shared by the in-game page (index.html) and the desktop probes (../../../stack_probe.cpp, fiber_probe.cpp).
(function () {
  'use strict';

  var tests = [];
  // expect: how a test should end ('RangeError' unless it says otherwise; a list = any of them).
  function test(id, name, body, expect) { tests.push({ id: id, name: name, body: body, expect: expect || 'RangeError' }); }

  test('plain', 'Plain recursion', function (d) {
    function f(n) { d.v = n; return f(n + 1) + 1; }
    return f(0);
  });

  test('frames', 'Large frames (many locals and arguments)', function (d) {
    function f(n, a, b, c, e, g, h, i) {
      var x1 = a + b, x2 = c + e, x3 = g + h, x4 = i + n, x5 = x1 * x2, x6 = x3 * x4;
      d.v = n;
      return f(n + 1, x1, x2, x3, x4, x5, x6, n) + x1 + x6;
    }
    return f(0, 1, 2, 3, 4, 5, 6, 7);
  });

  test('native', 'Through native calls (call, apply, Reflect.apply)', function (d) {
    function f(n) { d.v = n; return Reflect.apply(g, null, [n + 1]); }
    function g(n) { return f.call(null, n + 1) + h.apply(null, [n]); }
    function h(n) { return n & 1; }
    return f(0);
  });

  test('callbacks', 'Through array callbacks (map, reduce, sort)', function (d) {
    function f(n) {
      d.v = n;
      var a = [n, n + 1].sort(function (x, y) { return x - y; });
      return a.map(function (x) { return x === n ? f(n + 1) : 0; }).reduce(function (s, x) { return s + x; }, 0);
    }
    return f(0);
  });

  test('proxy', 'Through a Proxy trap and a getter', function (d) {
    var p = new Proxy({}, { get: function (t, k) { d.v++; return o.deep; } });
    var o = { get deep() { return p.x; } };
    return o.deep;
  });

  test('coerce', 'Through string coercion (toString, template literal)', function (d) {
    var o = { toString: function () { d.v++; return 'x' + String(q); } };
    var q = { toString: function () { return `${o}`; } };
    return String(o);
  });

  test('json', 'JSON.stringify of a 200,000-deep object', function (d) {
    var root = {}, node = root;
    for (var i = 0; i < 200000; i++) { node.c = {}; node = node.c; }
    d.v = 200000;
    return JSON.stringify(root).length;
  }, ['RangeError', 'finished']);   // JavaScriptCore stops it; other engines may finish it

  test('alloc', 'Allocating at every level (garbage collection near the limit)', function (d) {
    function f(n) {
      var junk = new Array(24).fill(n);
      var s = 'level ' + n + ' ' + junk.join(',');
      d.v = n;
      return f(n + 1) + junk.length + s.length;
    }
    return f(0);
  });

  // Runs code in a catch handler at each of the 2,000 levels nearest the limit, then ends normally.
  test('retry', 'Catch and retry at the limit', function (d) {
    var caught = 0;
    function g(n) {
      return [1, 2, 3, 4].map(function (x) { return x + n; }).filter(function (x) { return x & 1; }).join('-').length;
    }
    function f(n) {
      try { d.v = n; return f(n + 1) + 1; } catch (e) {
        caught++;
        try { g(n); } catch (e2) { /* the limit again */ }
        if (caught < 2000) throw e;
        return 0;
      }
    }
    try { return f(0); } finally { d.extra = caught + ' handlers ran'; }
  }, 'finished');

  test('errors', 'A new Error with its stack at every level while unwinding', function (d) {
    var made = 0;
    function f(n) {
      try { d.v = n; return f(n + 1) + 1; } catch (e) {
        if (n % 4) throw e;
        made++;
        var err = new RangeError('rethrown at ' + n + ' after ' + (e && e.message));
        void err.stack;
        throw err;
      }
    }
    try { return f(0); } finally { d.extra = made + ' errors built'; }
  });

  test('many', 'A ring of 300 different functions (compiled deep in the stack)', function (d) {
    var fns = [];
    for (var i = 0; i < 300; i++) {
      fns.push(new Function('n', 'fns', 'd', 'd.v = n; var k = n * ' + (i + 1) + ' & 7; ' +
        'return fns[(n + 1) % fns.length](n + 1, fns, d) + k;'));
    }
    return fns[0](0, fns, d);
  });

  // Layout forced from the catch handlers nearest the limit: engine work entered from a script at its deepest, the
  // case WebKit's own reserve was too small for (needs a page; ends normally).
  test('layout', 'Layout forced from the bottom of a recursion', function (d) {
    if (typeof document === 'undefined') { d.extra = 'no page here'; return 0; }
    var root = document.createElement('div'), node = root;
    for (var i = 0; i < 300; i++) {
      var c = document.createElement('div');
      c.style.padding = '1px';
      node.appendChild(c);
      node = c;
    }
    document.body.appendChild(root);
    var hits = 0;
    function f(n) {
      try { d.v = n; return f(n + 1) + 1; } catch (e) {
        root.style.width = (n % 50) + 'px';
        hits += root.offsetHeight > 0 ? 1 : 0;
        if (hits < 200) throw e;
        return 0;
      }
    }
    try { return f(0); } finally { root.remove(); d.extra = hits + ' layouts at the limit'; }
  }, 'finished');

  // A page nested this deep is laid out by the engine alone, with no script on the stack to stop it: only the stack
  // the pages run on (Magelight 1.3.0's own, "ulFiberMB") lets it through (needs a page; ends normally).
  [500, 1500].forEach(function (n) {
    test('dom' + n, 'Layout of a ' + n + '-level-deep page', function (d) {
      if (typeof document === 'undefined') { d.extra = 'no page here'; return 0; }
      var root = document.createElement('div'), node = root;
      for (var i = 0; i < n; i++) {
        var c = document.createElement('div');
        c.style.padding = '1px';
        node.appendChild(c);
        node = c;
      }
      document.body.appendChild(root);
      d.v = n;
      try { return root.offsetHeight; } finally { root.remove(); }
    }, 'finished');
  });

  // Runs one test; never throws. depth = how deep it got before it was stopped.
  function runOne(t) {
    var d = { v: 0, extra: '' };
    var t0 = Date.now(), outcome, message = '';
    try {
      t.body(d);
      outcome = 'finished';
    } catch (e) {
      outcome = (e && e.name) || 'error';
      message = String((e && e.message) || e).slice(0, 120);
    }
    return { id: t.id, name: t.name, outcome: outcome, expect: t.expect, message: message, depth: d.v,
             extra: d.extra, ms: Date.now() - t0 };
  }

  function passed(r) { return Array.isArray(r.expect) ? r.expect.indexOf(r.outcome) >= 0 : r.outcome === r.expect; }

  var root = typeof window !== 'undefined' ? window : this;
  root.MgStackTests = { tests: tests, runOne: runOne, passed: passed };
})();
