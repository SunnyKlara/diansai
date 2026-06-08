/* ============================================================
 * 电赛备赛刷题系统 - 单文件应用
 * 路由 / 状态 / 渲染 / 持久化 全部封装在此
 * ============================================================ */

const STORE_PREFIX = 'eddc:';
const Store = {
  get(key, def) {
    try {
      const v = localStorage.getItem(STORE_PREFIX + key);
      return v ? JSON.parse(v) : def;
    } catch { return def; }
  },
  set(key, val) {
    localStorage.setItem(STORE_PREFIX + key, JSON.stringify(val));
  },
  del(key) { localStorage.removeItem(STORE_PREFIX + key); },
  clearAll() {
    Object.keys(localStorage)
      .filter(k => k.startsWith(STORE_PREFIX))
      .forEach(k => localStorage.removeItem(k));
  }
};

const State = {
  questions: [],
  flashcards: [],
  problems: [],
  topics: [],
  done: new Set(Store.get('done', [])),
  wrong: new Set(Store.get('wrong', [])),
  star: new Set(Store.get('star', [])),
  streak: Store.get('streak', { days: 0, last: null }),
  // 当前刷题会话
  quizSession: null,
  // 比赛模式状态
  contests: Store.get('contests', {}),  // { "2024-H": { status, startedAt, elapsed, notes, revealed } }
};

function persist() {
  Store.set('done', [...State.done]);
  Store.set('wrong', [...State.wrong]);
  Store.set('star', [...State.star]);
  Store.set('streak', State.streak);
  Store.set('contests', State.contests);
  updateBadges();
}

/* ===== 比赛模式状态助手 ===== */
const CONTEST_DURATION_MS = (3 * 24 + 15) * 3600 * 1000; // 4天3夜从周三8点到周六11点 = 87h，按惯例 87h45m
function contestKey(p) { return `${p.year}-${p.problem}`; }
function getContest(p) {
  const k = contestKey(p);
  return State.contests[k] || { status: 'not_started', startedAt: null, elapsed: 0, notes: '', revealed: false };
}
function setContest(p, patch) {
  const k = contestKey(p);
  State.contests[k] = { ...getContest(p), ...patch };
  persist();
}
function contestStatusBadge(s) {
  return {
    not_started: { ico: '⬜', text: '未开始', color: 'var(--text-2)' },
    running:     { ico: '🔧', text: '进行中', color: 'var(--warn)' },
    submitted:   { ico: '✅', text: '已提交', color: 'var(--ok)' },
    abandoned:   { ico: '🏳️', text: '已放弃', color: 'var(--text-2)' },
  }[s] || { ico: '⬜', text: '未开始', color: 'var(--text-2)' };
}
function fmtDuration(ms) {
  if (ms < 0) ms = 0;
  const total = Math.floor(ms / 1000);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

function updateBadges() {
  document.getElementById('badge-wrong').textContent = State.wrong.size;
  document.getElementById('badge-star').textContent = State.star.size;
  document.querySelector('#streak-card .streak-num').textContent = State.streak.days;
}

function checkStreak() {
  const today = new Date().toISOString().slice(0, 10);
  if (State.streak.last === today) return;
  if (!State.streak.last) {
    State.streak = { days: 1, last: today };
  } else {
    const last = new Date(State.streak.last);
    const diff = (new Date(today) - last) / 86400000;
    State.streak = diff === 1
      ? { days: State.streak.days + 1, last: today }
      : { days: 1, last: today };
  }
  persist();
}

/* ===== Toast ===== */
function toast(msg, type = '') {
  const el = document.getElementById('toast');
  el.className = 'show ' + type;
  el.textContent = msg;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => el.className = '', 1800);
}

/* ===== 数据加载 ===== */
async function loadData() {
  const [q, f, p, t] = await Promise.all([
    fetch('./data/questions.json').then(r => r.json()),
    fetch('./data/flashcards.json').then(r => r.json()),
    fetch('./data/problems.json').then(r => r.json()),
    fetch('./data/topics.json').then(r => r.json()),
  ]);
  State.questions = q;
  State.flashcards = f;
  State.problems = p;
  State.topics = t;

  // 应用用户配置的 PDF 链接覆盖
  const overrides = Store.get('pdf_overrides', {});
  State.problems.forEach(prob => {
    const k = `${prob.year}-${prob.problem}`;
    if (overrides[k]) prob.pdf = overrides[k];
  });
}

/* ===== 路由 ===== */
const Routes = {};
function route(path, handler) { Routes[path] = handler; }

function navigate() {
  const hash = location.hash.replace(/^#/, '') || '/';
  const [path, ...rest] = hash.split('?');
  const params = new URLSearchParams(rest.join('?'));

  // 路由匹配
  let matchedKey = null;
  if (Routes[path]) matchedKey = path;
  else {
    const segs = path.split('/').filter(Boolean);
    while (segs.length > 0) {
      const key = '/' + segs.join('/');
      if (Routes[key]) { matchedKey = key; break; }
      segs.pop();
    }
  }
  const handler = matchedKey ? Routes[matchedKey] : Routes['/'];

  document.querySelectorAll('.nav-item').forEach(el => {
    el.classList.toggle('active', el.dataset.route === (matchedKey || '/'));
  });

  // 全屏模式（contest 比赛模式 + material 材料库左右栏）
  // /problem 题目分析中心保持普通布局（含主导航）
  document.body.classList.toggle('problem-mode',
    matchedKey === '/material' || matchedKey === '/contest');
  document.body.classList.toggle('contest-mode', matchedKey === '/contest');
  document.body.classList.toggle('analysis-mode', matchedKey === '/problem');

  // 关闭主导航抽屉
  closeDrawer();

  const app = document.getElementById('app');
  app.innerHTML = '';
  try {
    handler(app, params);
  } catch (e) {
    console.error(e);
    app.innerHTML = `<div class="empty"><div class="empty-ico">💥</div>渲染异常：${e.message}</div>`;
  }
  window.scrollTo(0, 0);
}

/* ===== 抽屉控制 ===== */
function openDrawer() {
  document.getElementById('sidebar').classList.add('open');
  document.getElementById('drawer-mask').classList.add('show');
  document.body.style.overflow = 'hidden';
}
function closeDrawer() {
  document.getElementById('sidebar').classList.remove('open');
  document.getElementById('drawer-mask').classList.remove('show');
  document.body.style.overflow = '';
}

window.addEventListener('hashchange', navigate);

/* ===== 工具 ===== */
function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const k in attrs) {
    if (k === 'class') node.className = attrs[k];
    else if (k === 'style') node.style.cssText = attrs[k];
    else if (k.startsWith('on')) node.addEventListener(k.slice(2).toLowerCase(), attrs[k]);
    else if (k === 'html') node.innerHTML = attrs[k];
    else node.setAttribute(k, attrs[k]);
  }
  for (const c of children.flat()) {
    if (c == null || c === false) continue;
    node.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
  }
  return node;
}

function shuffle(arr) {
  const a = arr.slice();
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

/* ============================================================
 * 路由：仪表盘
 * ============================================================ */
route('/', (app) => {
  const total = State.questions.length;
  const done = State.questions.filter(q => State.done.has(q.id)).length;
  const wrong = State.wrong.size;
  const star = State.star.size;
  const pct = total ? Math.round((done / total) * 100) : 0;

  app.appendChild(el('div', { class: 'page-title' }, '仪表盘'));
  app.appendChild(el('div', { class: 'page-sub' },
    `共 ${State.problems.length} 道真题、${State.questions.length} 道复习题、${State.flashcards.length} 张知识卡片`));

  // 顶部 4 张统计卡
  const topStats = el('div', { class: 'grid grid-4' },
    statCard('已答题', `${done} / ${total}`, `进度 ${pct}%`, pct),
    statCard('错题数', wrong, '点击侧栏 错题本 复习', null, '#/wrong'),
    statCard('收藏数', star, '点击侧栏 收藏夹 浏览', null, '#/star'),
    statCard('真题覆盖', `${State.problems.length} 题`, '近五年 2021-2025', null, '#/problems'),
  );
  app.appendChild(topStats);

  // 方向覆盖
  const sec1 = el('div', { class: 'section' });
  sec1.appendChild(el('h2', {}, '方向覆盖 ', el('span', { class: 'count' }, `（${State.topics.length} 个方向）`)));
  State.topics.forEach(t => {
    const tQuestions = State.questions.filter(q => q.topic === t.name);
    const tDone = tQuestions.filter(q => State.done.has(q.id)).length;
    const tPct = tQuestions.length ? Math.round((tDone / tQuestions.length) * 100) : 0;
    sec1.appendChild(el('div', { class: 'topic-row' },
      el('div', { class: 'topic-name' }, t.name),
      el('div', { class: 'topic-bar' }, el('div', { style: `width:${tPct}%` })),
      el('div', { class: 'topic-num' }, `${tDone}/${tQuestions.length}（${tPct}%）`),
    ));
  });
  app.appendChild(sec1);

  // 快捷入口
  const sec2 = el('div', { class: 'section' });
  sec2.appendChild(el('h2', {}, '快捷入口'));
  const grid = el('div', { class: 'grid grid-3' });
  [
    { ico: '📝', title: '随机刷题', sub: '从全部题库随机抽 10 题', href: '#/quiz?mode=random&n=10' },
    { ico: '❗', title: '专攻错题', sub: `${State.wrong.size} 道待复习`, href: '#/wrong' },
    { ico: '🃏', title: '知识卡片', sub: '快速过一遍要点', href: '#/cards' },
    { ico: '⭐', title: '收藏复盘', sub: `${State.star.size} 道难题`, href: '#/star' },
    { ico: '📚', title: '真题速查', sub: '24 道真题档案', href: '#/problems' },
    { ico: '🌳', title: '按方向训练', sub: '按 7 大方向刷', href: '#/topics' },
  ].forEach(item => {
    const a = el('a', { href: item.href, class: 'card', style: 'display:block;text-decoration:none;color:inherit;' },
      el('div', { style: 'font-size:24px;margin-bottom:6px;' }, item.ico),
      el('div', { style: 'font-weight:600;font-size:14px;' }, item.title),
      el('div', { style: 'color:var(--text-2);font-size:12px;margin-top:2px;' }, item.sub),
    );
    grid.appendChild(a);
  });
  sec2.appendChild(grid);
  app.appendChild(sec2);
});

function statCard(label, value, sub, pct, href) {
  const card = el('div', { class: 'stat' });
  card.appendChild(el('div', { class: 'stat-label' }, label));
  card.appendChild(el('div', { class: 'stat-value' }, String(value)));
  card.appendChild(el('div', { class: 'stat-sub' }, sub));
  if (pct != null) {
    card.appendChild(el('div', { class: 'progress-bar' },
      el('div', { class: 'progress-fill', style: `width:${pct}%` })));
  }
  if (href) {
    card.style.cursor = 'pointer';
    card.addEventListener('click', () => location.hash = href);
  }
  return card;
}

/* ============================================================
 * 路由：刷题模式
 * ============================================================ */
route('/quiz', (app, params) => {
  const mode = params.get('mode') || 'random';
  const n = parseInt(params.get('n') || '10', 10);
  const topic = params.get('topic') || '';
  const tag = params.get('tag') || '';
  const year = params.get('year') || '';

  // 过滤
  let pool = State.questions.slice();
  if (topic) pool = pool.filter(q => q.topic === topic);
  if (tag) pool = pool.filter(q => (q.tags || []).includes(tag));
  if (year) pool = pool.filter(q => String(q.year) === year);
  if (mode === 'wrong') pool = pool.filter(q => State.wrong.has(q.id));
  if (mode === 'star') pool = pool.filter(q => State.star.has(q.id));
  if (mode === 'undone') pool = pool.filter(q => !State.done.has(q.id));

  if (mode === 'random') pool = shuffle(pool).slice(0, Math.min(n, pool.length));
  else if (mode === 'wrong' || mode === 'star') pool = pool;
  else pool = pool.slice(0, Math.min(n, pool.length));

  if (pool.length === 0) {
    app.appendChild(el('div', { class: 'page-title' }, '刷题模式'));
    app.appendChild(el('div', { class: 'empty' },
      el('div', { class: 'empty-ico' }, '🍃'),
      '没有符合条件的题目。返回 ', el('a', { href: '#/' }, '仪表盘'), ' 调整筛选。'));
    return;
  }

  // 初始化或复用会话
  const sessionKey = JSON.stringify({ mode, n, topic, tag, year });
  if (!State.quizSession || State.quizSession.key !== sessionKey) {
    State.quizSession = {
      key: sessionKey,
      pool,
      idx: 0,
      answered: new Map(), // qid -> { user, correct }
    };
  }
  const sess = State.quizSession;

  app.appendChild(el('div', { class: 'page-title' }, '刷题模式'));
  app.appendChild(el('div', { class: 'page-sub' },
    `${modeLabel(mode)} · 共 ${sess.pool.length} 题 · 已答 ${sess.answered.size}`));

  // 工具栏
  const toolbar = el('div', { class: 'quiz-toolbar' });
  toolbar.appendChild(el('select', {
    onchange: (e) => location.hash = `#/quiz?mode=${e.target.value}`
  }, ...[
    ['random', '🎲 随机抽题'],
    ['undone', '🆕 未答题'],
    ['wrong', '❗ 错题本'],
    ['star', '⭐ 收藏夹'],
  ].map(([v, l]) => el('option', { value: v, ...(v === mode ? { selected: 'true' } : {}) }, l))));

  toolbar.appendChild(el('div', { class: 'info' }, `${sess.idx + 1} / ${sess.pool.length}`));
  app.appendChild(toolbar);

  // 渲染当前题
  renderQuiz(app, sess);
});

function modeLabel(m) {
  return { random: '随机抽题', undone: '未答题', wrong: '错题本', star: '收藏夹' }[m] || m;
}

function renderQuiz(app, sess) {
  const q = sess.pool[sess.idx];
  if (!q) return;

  const card = el('div', { class: 'quiz-card' });

  // 元信息
  const meta = el('div', { class: 'quiz-meta' });
  if (q.year) meta.appendChild(el('span', { class: 'tag year' }, `${q.year} ${q.problem || ''}`));
  if (q.topic) meta.appendChild(el('span', { class: 'tag' }, q.topic));
  if (q.subtopic) meta.appendChild(el('span', { class: 'tag' }, q.subtopic));
  if (q.difficulty) meta.appendChild(el('span', { class: `tag diff-${q.difficulty}` },
    ['', '入门', '进阶', '困难'][q.difficulty]));
  meta.appendChild(el('span', { class: 'tag' }, typeLabel(q.type)));
  (q.tags || []).slice(0, 3).forEach(t => meta.appendChild(el('span', { class: 'tag' }, '#' + t)));
  card.appendChild(meta);

  // 题面（支持 markdown 行内 code）
  card.appendChild(el('div', { class: 'quiz-question', html: renderInline(q.question) }));

  // 答题区
  const ans = sess.answered.get(q.id);
  const submitted = !!ans;

  if (q.type === 'fill') {
    const input = el('input', {
      type: 'text', class: 'fill-input',
      placeholder: '输入答案后按 回车 提交',
      ...(submitted ? { value: ans.user, disabled: 'true' } : {}),
    });
    if (!submitted) {
      input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' && input.value.trim()) {
          submitAnswer(q, input.value.trim(), sess, app);
        }
      });
    }
    card.appendChild(input);
  } else {
    const isMulti = q.type === 'multi';
    const userSet = new Set(submitted ? ans.user : []);
    const correctSet = new Set(q.answer);

    const optsBox = el('div', { class: 'options' });
    (q.options || (q.type === 'judge' ? ['正确', '错误'] : [])).forEach((opt, i) => {
      let cls = 'option';
      if (submitted) {
        if (correctSet.has(i)) cls += ' correct';
        else if (userSet.has(i)) cls += ' wrong';
      } else if (userSet.has(i)) {
        cls += ' selected';
      }
      const node = el('div', {
        class: cls,
        onclick: () => {
          if (submitted) return;
          if (isMulti) {
            userSet.has(i) ? userSet.delete(i) : userSet.add(i);
          } else {
            userSet.clear();
            userSet.add(i);
            // 单选/判断 即点即提交
            submitAnswer(q, [...userSet], sess, app);
            return;
          }
          renderQuiz(app, sess); // 重绘
          // 同步 userSet 状态到 sess（待提交）
          sess._draft = sess._draft || {};
          sess._draft[q.id] = [...userSet];
        },
      },
        el('div', { class: 'label' }, String.fromCharCode(65 + i)),
        el('div', { html: renderInline(opt) }),
      );
      optsBox.appendChild(node);
    });
    card.appendChild(optsBox);

    if (isMulti && !submitted) {
      const draft = (sess._draft && sess._draft[q.id]) || [];
      const submitBtn = el('button', {
        class: 'primary',
        onclick: () => {
          if (draft.length === 0) { toast('请先选择', 'err'); return; }
          submitAnswer(q, draft, sess, app);
        }
      }, '提交多选答案');
      card.appendChild(submitBtn);
    }
  }

  // 已答 → 解析
  if (submitted) {
    const okMark = ans.correct ? '✅ 正确' : '❌ 错误';
    const expBox = el('div', { class: 'explanation' });
    expBox.appendChild(el('div', { style: `font-weight:600;color:${ans.correct ? 'var(--ok)' : 'var(--error)'}` }, okMark));
    if (q.explanation) {
      expBox.appendChild(el('div', { style: 'margin-top:8px;', html: renderInline(q.explanation) }));
    }
    if (q.ref) {
      expBox.appendChild(el('div', { class: 'ref' }, '📂 参考：' + q.ref));
    }
    card.appendChild(expBox);
  }

  // 操作
  const actions = el('div', { class: 'quiz-actions' });
  actions.appendChild(el('button', {
    onclick: () => { if (sess.idx > 0) { sess.idx--; renderQuiz(app, sess); } },
    ...(sess.idx === 0 ? { disabled: 'true' } : {})
  }, '← 上一题'));

  if (submitted) {
    actions.appendChild(el('button', {
      class: 'primary',
      onclick: () => {
        if (sess.idx < sess.pool.length - 1) { sess.idx++; renderQuiz(app, sess); }
        else { showResult(app, sess); }
      }
    }, sess.idx === sess.pool.length - 1 ? '查看成绩' : '下一题 →'));
  } else {
    actions.appendChild(el('button', {
      onclick: () => { if (sess.idx < sess.pool.length - 1) { sess.idx++; renderQuiz(app, sess); } },
      ...(sess.idx === sess.pool.length - 1 ? { disabled: 'true' } : {})
    }, '跳过 →'));
  }

  actions.appendChild(el('button', {
    onclick: () => {
      if (State.star.has(q.id)) State.star.delete(q.id);
      else State.star.add(q.id);
      persist();
      renderQuiz(app, sess);
    }
  }, State.star.has(q.id) ? '★ 已收藏' : '☆ 收藏'));

  card.appendChild(actions);

  // 替换或追加
  const existing = app.querySelector('.quiz-card');
  if (existing) existing.replaceWith(card);
  else app.appendChild(card);
}

function typeLabel(t) {
  return { single: '单选', multi: '多选', judge: '判断', fill: '填空' }[t] || t;
}

function submitAnswer(q, user, sess, app) {
  const correct = checkAnswer(q, user);
  sess.answered.set(q.id, { user, correct });
  State.done.add(q.id);
  if (correct) {
    State.wrong.delete(q.id);
    toast('回答正确', 'ok');
  } else {
    State.wrong.add(q.id);
    toast('答错了，已加入错题本', 'err');
  }
  checkStreak();
  persist();
  renderQuiz(app, sess);
}

function checkAnswer(q, user) {
  if (q.type === 'fill') {
    const u = String(user).trim().toLowerCase();
    return (q.answer || []).some(a => u === String(a).trim().toLowerCase()
      || u.includes(String(a).trim().toLowerCase())
      || String(a).trim().toLowerCase().includes(u));
  }
  if (q.type === 'judge') {
    return Array.isArray(user) && user.length === 1 && user[0] === q.answer[0];
  }
  // single / multi
  const a = new Set(q.answer);
  const u = new Set(user);
  if (a.size !== u.size) return false;
  for (const x of a) if (!u.has(x)) return false;
  return true;
}

function showResult(app, sess) {
  const total = sess.pool.length;
  const correct = [...sess.answered.values()].filter(v => v.correct).length;
  const pct = Math.round((correct / total) * 100);

  app.innerHTML = '';
  app.appendChild(el('div', { class: 'page-title' }, '本轮成绩'));
  app.appendChild(el('div', { class: 'card', style: 'text-align:center;padding:40px;' },
    el('div', { style: `font-size:48px;color:${pct >= 80 ? 'var(--ok)' : pct >= 60 ? 'var(--warn)' : 'var(--error)'}` },
      pct >= 80 ? '🎉' : pct >= 60 ? '👍' : '💪'),
    el('div', { style: 'font-size:36px;font-weight:700;margin:10px 0;' }, `${correct} / ${total}`),
    el('div', { class: 'stat-sub' }, `正确率 ${pct}%`),
    el('div', { style: 'margin-top:24px;display:flex;gap:8px;justify-content:center;' },
      el('button', { class: 'primary', onclick: () => { State.quizSession = null; navigate(); } }, '再来一轮'),
      el('button', { onclick: () => location.hash = '#/wrong' }, '查看错题'),
      el('button', { onclick: () => location.hash = '#/' }, '返回首页'),
    ),
  ));
  State.quizSession = null;
}

/* ============================================================
 * 路由：知识卡片
 * ============================================================ */
route('/cards', (app, params) => {
  const topic = params.get('topic') || '';
  let cards = State.flashcards.slice();
  if (topic) cards = cards.filter(c => c.topic === topic);

  if (cards.length === 0) {
    app.appendChild(el('div', { class: 'page-title' }, '知识卡片'));
    app.appendChild(el('div', { class: 'empty' }, el('div', { class: 'empty-ico' }, '🃏'), '暂无卡片'));
    return;
  }

  if (typeof State._cardIdx !== 'number') State._cardIdx = 0;
  if (State._cardIdx >= cards.length) State._cardIdx = 0;

  app.appendChild(el('div', { class: 'page-title' }, '知识卡片'));
  app.appendChild(el('div', { class: 'page-sub' },
    `共 ${cards.length} 张 · 点击卡片翻面 · 左右方向键切换`));

  // topic 过滤
  const filter = el('div', { class: 'quiz-toolbar' });
  filter.appendChild(el('select', {
    onchange: (e) => { State._cardIdx = 0; location.hash = e.target.value ? `#/cards?topic=${e.target.value}` : '#/cards'; }
  },
    el('option', { value: '' }, '全部方向'),
    ...State.topics.map(t => el('option', {
      value: t.name, ...(t.name === topic ? { selected: 'true' } : {})
    }, t.name))
  ));
  filter.appendChild(el('div', { class: 'info' }, `${State._cardIdx + 1} / ${cards.length}`));
  app.appendChild(filter);

  const c = cards[State._cardIdx];
  const flip = el('div', { class: 'flashcard', onclick: () => flip.classList.toggle('flipped') },
    el('div', { class: 'flashcard-inner' },
      el('div', { class: 'flashcard-face flashcard-front' },
        el('div', { class: 'flashcard-title' }, c.front),
        el('div', { class: 'flashcard-content' }, '👆 点击翻面'),
        el('div', { class: 'flashcard-hint' }, `${c.topic || ''} ${c.tags ? '· ' + c.tags.join(' / ') : ''}`),
      ),
      el('div', { class: 'flashcard-face flashcard-back' },
        el('div', { class: 'flashcard-content', html: renderInline(c.back) }),
        c.ref ? el('div', { class: 'flashcard-hint' }, '📂 ' + c.ref) : null,
      ),
    )
  );
  app.appendChild(flip);

  app.appendChild(el('div', { class: 'card-nav' },
    el('button', {
      onclick: () => { State._cardIdx = (State._cardIdx - 1 + cards.length) % cards.length; navigate(); }
    }, '← 上一张'),
    el('button', {
      class: 'primary',
      onclick: () => { State._cardIdx = (State._cardIdx + 1) % cards.length; navigate(); }
    }, '下一张 →'),
    el('button', {
      onclick: () => { State._cardIdx = Math.floor(Math.random() * cards.length); navigate(); }
    }, '🎲 随机'),
  ));

  // 键盘
  if (!window._cardKey) {
    window._cardKey = true;
    document.addEventListener('keydown', (e) => {
      if (!location.hash.startsWith('#/cards')) return;
      if (e.key === 'ArrowLeft') {
        State._cardIdx = (State._cardIdx - 1 + State.flashcards.length) % State.flashcards.length;
        navigate();
      } else if (e.key === 'ArrowRight' || e.key === ' ') {
        State._cardIdx = (State._cardIdx + 1) % State.flashcards.length;
        navigate();
      } else if (e.key === 'Enter') {
        document.querySelector('.flashcard')?.classList.toggle('flipped');
      }
    });
  }
});

/* ============================================================
 * 路由：真题速查
 * ============================================================ */
route('/problems', (app) => {
  app.appendChild(el('div', { class: 'page-title' }, '真题速查'));
  app.appendChild(el('div', { class: 'page-sub' },
    `近五年（2021-2025）共 ${State.problems.length} 道真题档案`));

  // 按年份分组
  const byYear = {};
  State.problems.forEach(p => {
    (byYear[p.year] = byYear[p.year] || []).push(p);
  });

  Object.keys(byYear).sort().reverse().forEach(year => {
    const sec = el('div', { class: 'section' });
    sec.appendChild(el('h2', {}, `${year} 年`,
      el('span', { class: 'count' }, ` ${byYear[year].length} 题`)));
    const grid = el('div', { class: 'problem-grid' });
    byYear[year].forEach(p => {
      const c = getContest(p);
      const badge = contestStatusBadge(c.status);
      const item = el('a', {
        class: 'problem-item',
        href: `#/problem/${p.year}-${p.problem}`,
      },
        el('div', { class: 'problem-head' },
          el('span', { class: 'problem-id' }, `${p.year} ${p.problem}`),
          el('span', {
            class: 'problem-status',
            style: `color:${badge.color};`,
          }, `${badge.ico} ${badge.text}`),
        ),
        el('div', { class: 'problem-title' }, p.title),
        el('div', { class: 'problem-tech' },
          c.revealed
            ? '🔧 ' + (p.tech || '—')
            : '🔒 关键技术 / 解析 / 评分将在提交答卷后解锁'
        ),
        el('div', { style: 'margin-top:10px;display:flex;gap:6px;flex-wrap:wrap;' },
          el('span', { class: 'tag' }, p.topic || ''),
          p.platform && c.revealed ? el('span', { class: 'tag' }, p.platform) : null,
          p.level === 'S' && c.revealed ? el('span', { class: 'tag', style: 'color:#f0883e;border-color:#f0883e44;' }, '🌟 S 级') : null,
          p.pdf ? el('span', { class: 'tag', style: 'color:var(--accent-2);border-color:rgba(126,231,135,0.3);' }, '📄 真题PDF') : null,
          c.elapsed > 0 ? el('span', { class: 'tag', style: 'color:var(--warn);' }, '⏱ ' + fmtDuration(c.elapsed)) : null,
        ),
      );
      grid.appendChild(item);
    });
    sec.appendChild(grid);
    app.appendChild(sec);
  });
});

/* ============================================================
 * 路由：题目解读 #/problem/<year>-<id>
 * 目的：帮你看懂这道题。只放"读题辅助"的内容，不放任何工程方案/参考答案。
 * 工程方案（电路/代码/BOM/报告/经验）全部在 #/material 参考答案页。
 * ============================================================ */
route('/problem', async (app, params) => {
  const hash = location.hash.replace(/^#/, '').replace(/^\/problem\//, '');
  const [yearStr, problemId] = hash.split('-');
  const year = parseInt(yearStr, 10);
  const p = State.problems.find(x => x.year === year && x.problem === decodeURIComponent(problemId));
  if (!p) {
    app.appendChild(el('div', { class: 'page-title' }, '题目未找到'));
    app.appendChild(el('div', { class: 'empty' },
      el('div', { class: 'empty-ico' }, '🔍'),
      `没找到 ${year} 年 ${problemId} 题。`));
    return;
  }

  const c = getContest(p);
  const badge = contestStatusBadge(c.status);

  app.appendChild(el('a', { href: '#/problems', class: 'btn-back', style: 'display:inline-block;margin-bottom:14px;' }, '← 真题速查'));

  // 顶部题目信息卡
  const headCard = el('div', { class: 'card', style: 'margin-bottom:16px;' });
  headCard.appendChild(el('div', { style: 'display:flex;justify-content:space-between;align-items:flex-start;flex-wrap:wrap;gap:12px;' },
    el('div', { style: 'flex:1;min-width:200px;' },
      el('div', { style: 'display:flex;align-items:center;gap:10px;flex-wrap:wrap;margin-bottom:4px;' },
        el('span', { class: 'problem-id', style: 'font-size:13px;' }, `${p.year} 年 ${p.problem} 题`),
        el('span', { style: `color:${badge.color};font-size:12px;` }, `${badge.ico} ${badge.text}`),
        c.elapsed > 0 ? el('span', { style: 'font-size:11px;color:var(--warn);' },
          `⏱ ${fmtDuration(c.elapsed)} / 87:00:00`) : null,
      ),
      el('div', { style: 'font-size:18px;font-weight:600;margin-bottom:4px;' }, p.title),
      el('div', { style: 'font-size:12px;color:var(--text-2);' },
        `方向：${p.topic}` + (p.platform ? ` · 平台：${p.platform}` : '')),
    ),
    renderProblemActions(p, c),
  ));
  app.appendChild(headCard);

  // 主体：左 PDF + 右 题目解读
  const main = el('div', { class: 'analysis-layout' });
  app.appendChild(main);

  // 左：PDF
  const leftCol = el('div', { class: 'analysis-pdf-col' });
  if (p.pdf) {
    const pdfUrl = /^https?:\/\//i.test(p.pdf) ? p.pdf : '/' + p.pdf;
    leftCol.appendChild(el('div', { class: 'analysis-section-label' }, '📄 真题原件'));
    leftCol.appendChild(el('iframe', {
      class: 'analysis-pdf',
      src: pdfUrl + '#toolbar=1&navpanes=0&zoom=page-width',
      title: '真题 PDF',
    }));
    leftCol.appendChild(el('div', { style: 'margin-top:8px;text-align:right;' },
      el('a', { href: pdfUrl, target: '_blank', style: 'font-size:12px;color:var(--accent);' },
        '🔗 在新标签页打开')));
  } else if (p.origin) {
    leftCol.appendChild(el('div', { class: 'analysis-section-label' }, '📄 题目（文字版）'));
    const md = el('div', { class: 'markdown analysis-paper' });
    md.innerHTML = '<div class="loading">加载中…</div>';
    leftCol.appendChild(md);
    fetch('/' + p.origin).then(r => r.text()).then(t => {
      setMarkdownBase(p.origin.replace(/\/[^\/]+$/, ''));
      md.innerHTML = renderMarkdown(t);
    });
  }
  main.appendChild(leftCol);

  // 右：题目解读（小白友好版，只读题不给方案）
  const rightCol = el('div', { class: 'analysis-col' });
  rightCol.appendChild(el('div', { class: 'analysis-section-label' }, '📖 题目解读'));

  // 加载 brief 数据
  let briefs = State._briefs;
  if (!briefs) {
    try {
      const r = await fetch('./data/problem_briefs.json');
      briefs = State._briefs = r.ok ? await r.json() : {};
    } catch { briefs = State._briefs = {}; }
  }
  const brief = briefs[`${p.year}-${p.problem}`];

  if (!brief) {
    rightCol.appendChild(el('div', { class: 'brief-empty' },
      el('div', { style: 'font-size:36px;margin-bottom:8px;' }, '✍️'),
      el('div', { style: 'font-weight:600;margin-bottom:4px;' }, '题目解读尚未撰写'),
      el('div', { style: 'font-size:12px;color:var(--text-2);line-height:1.7;' },
        '该题的小白友好解读还没写。先看左侧 PDF 原件，',
        el('br'),
        '需要工程方案/参考答案，请到 ',
        el('a', { href: `#/material/${p.year}-${p.problem}`, style: 'color:var(--accent);' }, '参考答案页'),
        ' 查看。'),
    ));
    main.appendChild(rightCol);
    return;
  }

  // 渲染 brief 各模块
  if (brief.oneline) {
    rightCol.appendChild(el('div', { class: 'brief-card brief-oneline' },
      el('div', { class: 'brief-card-label' }, '🎯 一句话题目'),
      el('div', { class: 'brief-card-body' }, brief.oneline),
    ));
  }

  // 故事解读：叙事性长文，默认展开（让人愿意读下去）
  if (brief.narrative) {
    const narrCard = el('details', { class: 'brief-card brief-narrative', open: 'true' });
    narrCard.appendChild(el('summary', { class: 'narrative-head' },
      el('div', {},
        el('div', { class: 'narrative-title' }, '📖 故事解读'),
        el('div', { class: 'narrative-sub' }, '从背景讲起，看完你就懂了'),
      ),
      el('span', { class: 'narrative-chev' }, '▾'),
    ));
    const narrBody = el('div', { class: 'narrative-body' });
    setMarkdownBase('');
    narrBody.innerHTML = renderMarkdown(brief.narrative);
    narrCard.appendChild(narrBody);
    rightCol.appendChild(narrCard);
  }

  if (brief.why) {
    rightCol.appendChild(el('div', { class: 'brief-card brief-why' },
      el('div', { class: 'brief-card-label' }, '🤔 这题在干什么'),
      el('div', { class: 'brief-card-body' }, brief.why),
    ));
  }

  if (brief.tasks && brief.tasks.length) {
    const taskCard = el('div', { class: 'brief-card brief-tasks' });
    taskCard.appendChild(el('div', { class: 'brief-card-label' }, '📋 你需要做什么'));
    const list = el('ol', { class: 'brief-task-list' });
    brief.tasks.forEach(t => {
      const li = el('li', {});
      if (typeof t === 'string') {
        li.textContent = t;
      } else {
        li.appendChild(el('div', { class: 'brief-task-title' }, t.title));
        if (t.detail) li.appendChild(el('div', { class: 'brief-task-detail' }, t.detail));
        if (t.score != null) li.appendChild(el('div', { class: 'brief-task-score' }, `${t.score} 分`));
      }
      list.appendChild(li);
    });
    taskCard.appendChild(list);
    rightCol.appendChild(taskCard);
  }

  if (brief.metrics && brief.metrics.length) {
    const mcard = el('div', { class: 'brief-card brief-metrics' });
    mcard.appendChild(el('div', { class: 'brief-card-label' }, '📊 关键指标（一定要满足）'));
    const tbl = el('table', { class: 'brief-metric-table' });
    const head = el('tr', {});
    head.appendChild(el('th', {}, '指标'));
    head.appendChild(el('th', {}, '要求'));
    head.appendChild(el('th', {}, '通俗理解'));
    tbl.appendChild(head);
    brief.metrics.forEach(m => {
      const tr = el('tr', {});
      tr.appendChild(el('td', {}, m.name));
      tr.appendChild(el('td', { class: 'cell-strong' }, m.spec));
      tr.appendChild(el('td', { class: 'cell-soft' }, m.plain || ''));
      tbl.appendChild(tr);
    });
    mcard.appendChild(tbl);
    rightCol.appendChild(mcard);
  }

  if (brief.traps && brief.traps.length) {
    const trapCard = el('div', { class: 'brief-card brief-traps' });
    trapCard.appendChild(el('div', { class: 'brief-card-label' }, '⚠️ 容易掉的坑'));
    brief.traps.forEach(t => {
      trapCard.appendChild(el('div', { class: 'brief-trap-item' },
        el('div', { class: 'brief-trap-title' }, '❌ ' + t.what),
        el('div', { class: 'brief-trap-fix' }, '✅ ' + t.fix),
      ));
    });
    rightCol.appendChild(trapCard);
  }

  if (brief.checklist && brief.checklist.length) {
    const ck = el('div', { class: 'brief-card brief-checklist' });
    ck.appendChild(el('div', { class: 'brief-card-label' }, '✅ 审题自查清单'));
    const ul = el('ul', { class: 'brief-check-list' });
    brief.checklist.forEach(item => {
      ul.appendChild(el('li', {}, item));
    });
    ck.appendChild(ul);
    rightCol.appendChild(ck);
  }

  // 底部链接到完整工程方案
  rightCol.appendChild(el('div', { class: 'brief-cta' },
    el('div', { style: 'font-size:12px;color:var(--text-2);margin-bottom:8px;' },
      '✏️ 看完题目想动手了？以下两条路：'),
    el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;' },
      el('a', {
        class: 'pdf-row-btn primary', href: `#/contest/${p.year}-${p.problem}`,
        onclick: (e) => {
          if (c.status === 'not_started') {
            e.preventDefault();
            if (confirm('开始 87 小时比赛模式？\n\n（取消则继续浏览）')) {
              setContest(p, { status: 'running', startedAt: Date.now(), elapsed: 0 });
              location.hash = `#/contest/${p.year}-${p.problem}`;
            }
          }
        }
      }, c.status === 'running' ? '🔧 继续比赛' : '🚀 开始独立做题'),
      el('a', {
        class: 'pdf-row-btn',
        href: `#/material/${p.year}-${p.problem}`,
        onclick: (e) => {
          if (!c.revealed) {
            if (!confirm('参考答案含完整工程方案。看了之后将解锁解析。\n确认要看吗？')) {
              e.preventDefault();
              return;
            }
            setContest(p, { revealed: true });
          }
        }
      }, '📚 看参考答案'),
    ),
  ));

  main.appendChild(rightCol);
});

function renderProblemActions(p, c) {
  const wrap = el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;' });
  if (c.status === 'not_started') {
    wrap.appendChild(el('button', {
      class: 'primary',
      onclick: () => {
        if (confirm(`开始 ${p.year} ${p.problem} 题比赛模式？\n\n开始后启动 87 小时倒计时。\n（取消则只浏览，不计时）`)) {
          setContest(p, { status: 'running', startedAt: Date.now(), elapsed: 0 });
          location.hash = `#/contest/${p.year}-${p.problem}`;
        }
      }
    }, '🚀 开始比赛'));
  } else if (c.status === 'running') {
    wrap.appendChild(el('button', {
      class: 'primary',
      onclick: () => location.hash = `#/contest/${p.year}-${p.problem}`,
    }, '🔧 继续比赛'));
    wrap.appendChild(el('button', {
      onclick: () => {
        if (confirm('确认提交答卷？提交后比赛结束。')) {
          setContest(p, { status: 'submitted', revealed: true });
          navigate();
        }
      }
    }, '✅ 提交'));
  } else {
    wrap.appendChild(el('button', {
      class: 'primary',
      onclick: () => location.hash = `#/material/${p.year}-${p.problem}`,
    }, '📚 参考答案'));
    wrap.appendChild(el('button', {
      onclick: () => {
        if (confirm(`重置 ${p.year} ${p.problem} 题进度？所有计时和笔记将清空。`)) {
          setContest(p, { status: 'not_started', startedAt: null, elapsed: 0, notes: '', revealed: false });
          navigate();
        }
      }
    }, '🔁 重置'));
  }
  return wrap;
}

/* ============================================================
 * 路由：比赛模式 #/contest/<year>-<id>
 * 全屏沉浸式，只显示题目，含计时器和笔记
 * ============================================================ */
route('/contest', (app, params) => {
  const hash = location.hash.replace(/^#/, '').replace(/^\/contest\//, '');
  const [yearStr, problemId] = hash.split('-');
  const year = parseInt(yearStr, 10);
  const p = State.problems.find(x => x.year === year && x.problem === decodeURIComponent(problemId));
  if (!p) { location.hash = '#/problems'; return; }

  let c = getContest(p);

  // 首次进入：尚未开始就直接重定向
  if (c.status === 'not_started') {
    location.hash = `#/problem/${p.year}-${p.problem}`;
    return;
  }
  // 已提交/放弃：重定向到材料页
  if (c.status === 'submitted' || c.status === 'abandoned') {
    location.hash = `#/material/${p.year}-${p.problem}`;
    return;
  }

  // 当前用时（实时）
  function currentElapsed() {
    return c.elapsed + (c.startedAt ? Date.now() - c.startedAt : 0);
  }

  const root = el('div', { class: 'contest-page' });

  // 顶部计时栏
  const head = el('div', { class: 'contest-head' });
  head.appendChild(el('button', {
    class: 'btn-back',
    onclick: () => {
      // 暂存进度然后退出
      setContest(p, { elapsed: currentElapsed(), startedAt: Date.now() });
      location.hash = `#/problem/${p.year}-${p.problem}`;
    }
  }, '← 退出（保留进度）'));
  const timerDisplay = el('div', { class: 'contest-timer' });
  head.appendChild(timerDisplay);

  const submitBtn = el('button', {
    class: 'primary',
    onclick: () => {
      if (confirm(`确认提交 ${p.year} ${p.problem} 题答卷？\n用时 ${fmtDuration(currentElapsed())}\n提交后解锁解析。`)) {
        setContest(p, { status: 'submitted', elapsed: currentElapsed(), startedAt: null, revealed: true });
        location.hash = `#/material/${p.year}-${p.problem}`;
      }
    }
  }, '✅ 提交');
  head.appendChild(submitBtn);
  root.appendChild(head);

  // 题目主体
  const body = el('div', { class: 'contest-body' });
  body.appendChild(el('div', { style: 'display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:8px;margin-bottom:14px;' },
    el('div', {},
      el('div', { style: 'font-size:13px;color:var(--text-2);' }, `${p.year} 年全国大学生电子设计竞赛`),
      el('div', { style: 'font-size:22px;font-weight:600;margin-top:4px;' }, `${p.problem} 题：${p.title}`),
    ),
  ));
  body.appendChild(el('div', { style: 'border-top:1px solid var(--border);margin-bottom:14px;' }));

  // 题目主体 - PDF 优先（含官方原图），markdown 文字版作为兜底
  const pdfPath = p.pdf;
  const originPath = p.origin;

  if (pdfPath) {
    // PDF 真题原件嵌入（最优）
    const pdfUrl = /^https?:\/\//i.test(pdfPath) ? pdfPath : '/' + pdfPath;
    const pdfBox = el('div', { class: 'contest-pdf-wrap' });
    pdfBox.appendChild(el('iframe', {
      class: 'contest-pdf',
      src: pdfUrl + (pdfUrl.includes('#') ? '' : '#toolbar=1&navpanes=0&zoom=page-width'),
      title: '真题原件 PDF',
    }));
    pdfBox.appendChild(el('div', { class: 'pdf-actions' },
      el('a', { href: pdfUrl, target: '_blank', class: 'pdf-row-btn primary' }, '🔍 在新标签页打开'),
      originPath ? el('a', {
        href: '#', class: 'pdf-row-btn',
        onclick: (e) => {
          e.preventDefault();
          // 切换到文字版
          const fb = pdfBox.nextSibling;
          if (fb) fb.style.display = (fb.style.display === 'none' ? 'block' : 'none');
        },
      }, '📝 切换文字版') : null,
    ));
    body.appendChild(pdfBox);

    // 文字版（默认隐藏，作为兜底）
    if (originPath) {
      const fallback = el('div', { class: 'contest-paper', style: 'display:none;margin-top:16px;' });
      fallback.innerHTML = '<div class="loading">加载文字版…</div>';
      body.appendChild(fallback);
      fetch('/' + originPath).then(r => r.text()).then(text => {
        fallback.innerHTML = '';
        const md = el('div', { class: 'markdown' });
        setMarkdownBase(originPath.replace(/\/[^\/]+$/, ''));
        md.innerHTML = renderMarkdown(text);
        fallback.appendChild(md);
      }).catch(e => {
        fallback.innerHTML = '<div class="empty">加载失败：' + e.message + '</div>';
      });
    }
  } else if (originPath) {
    // 没 PDF 只有文字版
    const mdContainer = el('div', { class: 'contest-paper' });
    mdContainer.innerHTML = '<div class="loading">加载题目…</div>';
    body.appendChild(mdContainer);
    fetch('/' + originPath).then(r => r.text()).then(text => {
      mdContainer.innerHTML = '';
      const md = el('div', { class: 'markdown' });
      setMarkdownBase(originPath.replace(/\/[^\/]+$/, ''));
      md.innerHTML = renderMarkdown(text);
      mdContainer.appendChild(md);
    }).catch(e => {
      mdContainer.innerHTML = '<div class="empty">加载失败：' + e.message + '</div>';
    });
  } else {
    body.appendChild(el('div', { class: 'card', style: 'padding:30px 20px;text-align:center;' },
      el('div', { style: 'font-size:36px;margin-bottom:10px;' }, '📄'),
      el('div', { style: 'font-size:15px;font-weight:600;margin-bottom:8px;' }, '该题题目原件暂未入库'),
      el('div', { style: 'font-size:13px;color:var(--text-2);line-height:1.7;' },
        '请将真题文字版放在 ',
        el('code', {}, p.path + '00_题目原件.md'),
        ' 即可显示。'),
    ));
  }
  root.appendChild(body);

  // 笔记区（折叠抽屉）
  const notesBar = el('div', { class: 'contest-notes-bar' });
  const notesToggle = el('button', { class: 'notes-toggle' }, '📝 解题笔记 ' + (c.notes ? `(${c.notes.length} 字)` : ''));
  notesBar.appendChild(notesToggle);
  notesBar.appendChild(el('div', { style: 'flex:1;font-size:11px;color:var(--text-2);text-align:right;' },
    '所有笔记仅保存在本地浏览器'));
  root.appendChild(notesBar);

  const notesPanel = el('div', { class: 'contest-notes-panel' });
  const notesArea = el('textarea', {
    class: 'notes-area',
    placeholder: '在此记录你的解题思路、电路方案、调试日志…\n\n建议结构：\n1) 审题 / 指标分解\n2) 方案选型\n3) 硬件 BOM\n4) 软件流程\n5) 测试结果',
    rows: 14,
  });
  notesArea.value = c.notes || '';
  notesArea.addEventListener('input', () => {
    setContest(p, { notes: notesArea.value });
    notesToggle.textContent = `📝 解题笔记 (${notesArea.value.length} 字)`;
  });
  notesPanel.appendChild(notesArea);
  root.appendChild(notesPanel);

  notesToggle.addEventListener('click', () => {
    notesPanel.classList.toggle('open');
    notesToggle.textContent = (notesPanel.classList.contains('open') ? '📕 收起笔记' : '📝 解题笔记') +
      ` (${notesArea.value.length} 字)`;
  });

  app.appendChild(root);

  // 计时器
  function tick() {
    const elapsed = currentElapsed();
    const remaining = CONTEST_DURATION_MS - elapsed;
    timerDisplay.innerHTML = `
      <div class="timer-elapsed">⏱ ${fmtDuration(elapsed)}</div>
      <div class="timer-remaining">剩余 ${fmtDuration(remaining)}</div>`;
    if (remaining <= 0) {
      // 时间到 → 自动提交
      clearInterval(State._contestTimer);
      setContest(p, { status: 'submitted', elapsed: CONTEST_DURATION_MS, startedAt: null, revealed: true });
      alert('比赛时间到，已自动提交。');
      location.hash = `#/material/${p.year}-${p.problem}`;
    }
  }
  tick();
  if (State._contestTimer) clearInterval(State._contestTimer);
  State._contestTimer = setInterval(tick, 1000);

  // 退出页面时停止计时器并保存
  window.addEventListener('hashchange', function once() {
    clearInterval(State._contestTimer);
    if (c.status === 'running') {
      setContest(p, { elapsed: currentElapsed(), startedAt: Date.now() });
    }
    window.removeEventListener('hashchange', once);
  });
});

/* ============================================================
 * 路由：作战材料库（解锁后） #/material/<year>-<id>
 * 完整 markdown 文件浏览（旧 /problem 页改名）
 * ============================================================ */
route('/material', async (app, params) => {
  const hash = location.hash.replace(/^#/, '').replace(/^\/material\//, '');
  const [yearStr, problemId] = hash.split('-');
  const year = parseInt(yearStr, 10);
  const p = State.problems.find(x => x.year === year && x.problem === decodeURIComponent(problemId));
  if (!p) { location.hash = '#/problems'; return; }

  const c = getContest(p);
  if (!c.revealed) {
    // 未解锁 → 跳到题目入口
    location.hash = `#/problem/${p.year}-${p.problem}`;
    return;
  }

  // 标准文件清单
  const candidates = [
    { key: 'guide',   label: '📘 做题指南',         file: '00_做题指南.md' },
    { key: 'review',  label: '🔍 深度审题与方案论证', file: '00_深度审题与方案论证.md' },
    { key: 'replica', label: '🌟 完整复刻指南',     file: '05_完整复刻指南.md' },
    { key: 'codeReadme', label: '💻 代码 README',  file: '01_代码/README.md' },
    { key: 'config',  label: '⚙️ config.h',         file: '01_代码/config.h' },
    { key: 'circuit', label: '🔌 电路设计说明',     file: '02_硬件/电路设计说明.md' },
    { key: 'bom',     label: '📦 材料清单 BOM',     file: '02_硬件/材料清单_BOM.md' },
    { key: 'report',  label: '📝 设计报告',         file: '03_报告/设计报告.md' },
    { key: 'testdata',label: '📊 测试数据',         file: '03_报告/测试数据模板.md' },
    { key: 'checklist', label: '✅ 调试检查清单',   file: '04_调试记录/调试检查清单.md' },
    { key: 'lessons', label: '💡 独到经验总结',     file: '04_调试记录/独到经验总结.md' },
  ];
  const base = p.path.replace(/\/$/, '') + '/';

  const root = el('div', { class: 'problem-page' });

  const head = el('div', { class: 'problem-page-head' });
  head.appendChild(el('a', { href: `#/problem/${p.year}-${p.problem}`, class: 'btn-back' }, '←'));
  head.appendChild(el('div', { class: 'problem-page-title' },
    el('div', { class: 'pp-title' }, `${p.year} ${p.problem} · ${p.title}`),
    el('div', { class: 'pp-sub' },
      `${c.status === 'submitted' ? '✅ 用时 ' + fmtDuration(c.elapsed) : '🏳️ 已放弃'} · ${p.topic}${p.platform ? ' · ' + p.platform : ''}${p.score ? ' · ' + p.score + '/40' : ''}`),
  ));
  head.appendChild(el('button', {
    class: 'btn-toc',
    onclick: () => document.querySelector('.problem-side').classList.toggle('open')
  }, '📑 目录'));
  root.appendChild(head);

  const layout = el('div', { class: 'problem-layout' });
  const sideNav = el('aside', { class: 'problem-side' });
  const content = el('main', { class: 'problem-content' });
  layout.appendChild(sideNav);
  layout.appendChild(content);
  root.appendChild(layout);
  app.appendChild(root);

  // 探测可用文件
  sideNav.appendChild(el('div', { class: 'side-section-label' }, '检测中…'));
  const available = [];
  await Promise.all(candidates.map(async (cand) => {
    try {
      const r = await fetch('/' + base + cand.file);
      if (r.ok) available.push(cand);
    } catch {}
  }));
  available.sort((a, b) =>
    candidates.findIndex(x => x.key === a.key) - candidates.findIndex(x => x.key === b.key));

  sideNav.innerHTML = '';

  // 用户笔记入口（如果有）
  if (c.notes && c.notes.trim()) {
    sideNav.appendChild(el('div', { class: 'side-section-label' }, '🎯 我的解答'));
    sideNav.appendChild(el('a', {
      class: 'side-item active', href: '#', 'data-key': '_my_notes',
      onclick: (e) => { e.preventDefault(); selectMyNotes(); }
    }, '📝 比赛笔记'));
  }

  if (available.length) {
    sideNav.appendChild(el('div', { class: 'side-section-label', style: 'margin-top:12px;' }, '📚 标准解析'));
    available.forEach((cand) => {
      sideNav.appendChild(el('a', {
        class: 'side-item',
        href: '#', 'data-key': cand.key,
        onclick: (e) => { e.preventDefault(); selectFile(cand); }
      }, cand.label));
    });
  } else {
    sideNav.appendChild(el('div', { class: 'side-empty' },
      '该题的标准解析文件未在仓库中找到。',
      el('div', { style: 'margin-top:6px;font-size:11px;' }, '路径：',
        el('code', {}, base))));
  }

  // 工程目录链接
  sideNav.appendChild(el('div', { class: 'side-section-label', style: 'margin-top:12px;' }, '🔧 工程目录'));
  ['01_代码/', '02_硬件/', '03_报告/', '04_调试记录/'].forEach(f => {
    sideNav.appendChild(el('a', {
      class: 'side-item small',
      href: '/' + base + f, target: '_blank',
    }, '📁 ' + f));
  });

  // 默认显示笔记 → 否则第一个文件
  if (c.notes && c.notes.trim()) selectMyNotes();
  else if (available.length) selectFile(available[0]);
  else content.appendChild(el('div', { class: 'empty' },
    el('div', { class: 'empty-ico' }, '📁'), '暂无解析文件'));

  function selectMyNotes() {
    sideNav.querySelectorAll('.side-item').forEach(el => el.classList.remove('active'));
    const cur = sideNav.querySelector('.side-item[data-key="_my_notes"]');
    if (cur) cur.classList.add('active');
    if (window.innerWidth < 768) sideNav.classList.remove('open');

    content.innerHTML = '';
    content.appendChild(el('div', { class: 'file-bar' },
      el('div', {}, '📝 我的比赛笔记'),
      el('div', { class: 'file-bar-path' }, `用时 ${fmtDuration(c.elapsed)} · ${c.notes.length} 字`),
    ));
    content.appendChild(el('pre', {
      style: 'background:var(--panel);border:1px solid var(--border);padding:16px;border-radius:8px;white-space:pre-wrap;font-family:inherit;font-size:14px;line-height:1.7;'
    }, c.notes));
    content.scrollTop = 0;
  }

  async function selectFile(cand) {
    sideNav.querySelectorAll('.side-item').forEach(el => el.classList.remove('active'));
    const cur = sideNav.querySelector(`.side-item[data-key="${cand.key}"]`);
    if (cur) cur.classList.add('active');
    if (window.innerWidth < 768) sideNav.classList.remove('open');

    content.innerHTML = '<div class="loading">加载中…</div>';
    try {
      const url = '/' + base + cand.file;
      const r = await fetch(url);
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const text = await r.text();
      content.innerHTML = '';
      content.appendChild(el('div', { class: 'file-bar' },
        el('div', {}, cand.label),
        el('div', { class: 'file-bar-path' }, base + cand.file),
      ));
      const md = el('div', { class: 'markdown' });
      if (cand.file.endsWith('.h') || cand.file.endsWith('.c')) {
        md.appendChild(el('pre', {}, el('code', {}, text)));
      } else {
        // 设置当前文件所在目录，让 markdown 里的相对路径图片能加载
        const fileDir = (base + cand.file).replace(/\/[^\/]+$/, '');
        setMarkdownBase(fileDir);
        md.innerHTML = renderMarkdown(text);
      }
      content.appendChild(md);
      content.scrollTop = 0;
    } catch (e) {
      content.innerHTML = '';
      content.appendChild(el('div', { class: 'empty' },
        el('div', { class: 'empty-ico' }, '⚠️'),
        '加载失败：' + e.message));
    }
  }
});


/* ===== 极简 Markdown 渲染器 ===== */
// 当前正在渲染的 markdown 文件目录（用于解析相对路径的图片/链接）
let _mdBaseDir = '';
function setMarkdownBase(dir) { _mdBaseDir = dir || ''; }
function resolveAssetUrl(src) {
  // 绝对 URL / 站内绝对路径 / data: → 不动
  if (/^([a-z]+:|\/|#|data:)/i.test(src)) return src;
  // 相对路径：拼到当前 md 所在目录
  if (!_mdBaseDir) return src;
  // 处理 ../
  let base = _mdBaseDir.replace(/\/+$/, '');
  let rel = src;
  while (rel.startsWith('../')) {
    base = base.replace(/\/[^\/]+$/, '');
    rel = rel.slice(3);
  }
  return '/' + base + '/' + rel;
}

function renderMarkdown(src) {
  // 0. 转义
  let s = src.replace(/\r\n/g, '\n');

  // 1. 提取代码块（fenced）
  const codes = [];
  s = s.replace(/```([a-zA-Z]*)\n([\s\S]*?)```/g, (m, lang, code) => {
    codes.push({ lang, code });
    return `\u0000CODE${codes.length - 1}\u0000`;
  });

  // 2. 转义剩余 HTML
  s = s.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));

  // 3. 表格
  s = s.replace(/((?:^\|.+\|\n)+)/gm, (block) => {
    const lines = block.trim().split('\n');
    if (lines.length < 2 || !/^\|[\s\-:|]+\|$/.test(lines[1])) return block;
    const head = lines[0].split('|').slice(1, -1).map(c => c.trim());
    const rows = lines.slice(2).map(l => l.split('|').slice(1, -1).map(c => c.trim()));
    let html = '<table><thead><tr>';
    head.forEach(h => html += `<th>${h}</th>`);
    html += '</tr></thead><tbody>';
    rows.forEach(r => {
      html += '<tr>';
      r.forEach(c => html += `<td>${c}</td>`);
      html += '</tr>';
    });
    html += '</tbody></table>';
    return html + '\n';
  });

  // 4. 标题
  s = s.replace(/^###### (.*)$/gm, '<h6>$1</h6>');
  s = s.replace(/^##### (.*)$/gm, '<h5>$1</h5>');
  s = s.replace(/^#### (.*)$/gm, '<h4>$1</h4>');
  s = s.replace(/^### (.*)$/gm, '<h3>$1</h3>');
  s = s.replace(/^## (.*)$/gm, '<h2>$1</h2>');
  s = s.replace(/^# (.*)$/gm, '<h1>$1</h1>');

  // 5. 引用
  s = s.replace(/^&gt; (.*)$/gm, '<blockquote>$1</blockquote>');

  // 6. 列表
  s = s.replace(/^(?:[-*] .+\n?)+/gm, (block) => {
    const items = block.trim().split('\n').map(l => l.replace(/^[-*] /, ''));
    return '<ul>' + items.map(i => `<li>${i}</li>`).join('') + '</ul>\n';
  });
  s = s.replace(/^(?:\d+\. .+\n?)+/gm, (block) => {
    const items = block.trim().split('\n').map(l => l.replace(/^\d+\. /, ''));
    return '<ol>' + items.map(i => `<li>${i}</li>`).join('') + '</ol>\n';
  });

  // 7. 行内：加粗 / 斜体 / 行内 code / 图片 / 链接
  s = s.replace(/`([^`]+)`/g, '<code>$1</code>');
  s = s.replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>');
  s = s.replace(/(?<!\*)\*([^*\n]+)\*(?!\*)/g, '<em>$1</em>');
  // 图片：![alt](path) —— 必须在普通链接之前
  s = s.replace(/!\[([^\]]*)\]\(([^)]+)\)/g,
    (_, alt, src) => `<img src="${resolveAssetUrl(src)}" alt="${alt}" loading="lazy" />`);
  s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank">$1</a>');

  // 8. 段落（简单：连续非块级行包成 p）
  s = s.split(/\n{2,}/).map(block => {
    const t = block.trim();
    if (!t) return '';
    if (/^<(h\d|ul|ol|li|blockquote|pre|table|thead|tbody|tr|td|th|p|div)/i.test(t)) return t;
    if (/^\u0000CODE\d+\u0000$/.test(t)) return t;
    return '<p>' + t.replace(/\n/g, '<br>') + '</p>';
  }).join('\n\n');

  // 9. 还原代码块
  s = s.replace(/\u0000CODE(\d+)\u0000/g, (_, i) => {
    const { lang, code } = codes[+i];
    const safe = code.replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
    return `<pre><code class="lang-${lang}">${safe}</code></pre>`;
  });

  return s;
}

/* ============================================================
 * 路由：方向树
 * ============================================================ */
route('/topics', (app) => {
  app.appendChild(el('div', { class: 'page-title' }, '方向树'));
  app.appendChild(el('div', { class: 'page-sub' }, '按方向 / 标签筛选刷题'));

  const list = el('div', { class: 'topic-tree' });
  State.topics.forEach(t => {
    const qs = State.questions.filter(q => q.topic === t.name);
    const tagSet = new Set();
    qs.forEach(q => (q.tags || []).forEach(x => tagSet.add(x)));
    const tags = [...tagSet].sort();

    const block = el('div', { class: 'topic-block' });
    block.appendChild(el('div', { class: 'topic-block-head' },
      el('div', { class: 'topic-block-title' }, t.name),
      el('div', { class: 'topic-block-count' },
        `${qs.length} 题 · ${tags.length} 标签`),
    ));
    if (t.desc) block.appendChild(el('div', { class: 'page-sub', style: 'margin-bottom:10px;' }, t.desc));

    const tagList = el('div', { class: 'tag-list' });
    tagList.appendChild(el('span', {
      class: 'tag', style: 'cursor:pointer;color:var(--accent);border-color:var(--accent);',
      onclick: () => location.hash = `#/quiz?mode=undone&topic=${encodeURIComponent(t.name)}&n=20`
    }, `🚀 刷该方向（${qs.length} 题）`));
    tags.forEach(tag => {
      tagList.appendChild(el('span', {
        class: 'tag',
        onclick: () => location.hash = `#/quiz?mode=undone&tag=${encodeURIComponent(tag)}&n=20`
      }, '#' + tag));
    });
    block.appendChild(tagList);
    list.appendChild(block);
  });
  app.appendChild(list);
});

/* ============================================================
 * 路由：错题本
 * ============================================================ */
route('/wrong', (app) => {
  app.appendChild(el('div', { class: 'page-title' }, '错题本'));
  app.appendChild(el('div', { class: 'page-sub' }, `共 ${State.wrong.size} 道答错的题目`));

  if (State.wrong.size === 0) {
    app.appendChild(el('div', { class: 'empty' }, el('div', { class: 'empty-ico' }, '✨'), '错题本是空的，干得漂亮'));
    return;
  }

  app.appendChild(el('div', { style: 'display:flex;gap:8px;margin-bottom:18px;' },
    el('button', { class: 'primary', onclick: () => location.hash = '#/quiz?mode=wrong' }, '🔁 一键重刷错题'),
    el('button', { class: 'danger', onclick: () => {
      if (confirm('确认清空错题本？')) { State.wrong.clear(); persist(); navigate(); }
    } }, '清空'),
  ));

  const list = el('div', {});
  [...State.wrong].forEach(qid => {
    const q = State.questions.find(x => x.id === qid);
    if (!q) return;
    list.appendChild(el('div', { class: 'search-result-item', onclick: () => {
      State.quizSession = { key: 'one-' + qid, pool: [q], idx: 0, answered: new Map() };
      location.hash = '#/quiz?mode=wrong';
    } },
      el('div', { class: 'quiz-meta' },
        q.year ? el('span', { class: 'tag year' }, `${q.year} ${q.problem}`) : null,
        q.topic ? el('span', { class: 'tag' }, q.topic) : null,
        el('span', { class: `tag diff-${q.difficulty || 1}` }, ['', '入门', '进阶', '困难'][q.difficulty || 1]),
      ),
      el('div', { html: renderInline(q.question.slice(0, 120) + (q.question.length > 120 ? '…' : '')) }),
    ));
  });
  app.appendChild(list);
});

/* ============================================================
 * 路由：收藏夹
 * ============================================================ */
route('/star', (app) => {
  app.appendChild(el('div', { class: 'page-title' }, '收藏夹'));
  app.appendChild(el('div', { class: 'page-sub' }, `共 ${State.star.size} 道收藏的题目`));

  if (State.star.size === 0) {
    app.appendChild(el('div', { class: 'empty' }, el('div', { class: 'empty-ico' }, '⭐'),
      '还没收藏任何题目。在刷题时点击 ☆ 即可收藏'));
    return;
  }

  app.appendChild(el('div', { style: 'margin-bottom:18px;' },
    el('button', { class: 'primary', onclick: () => location.hash = '#/quiz?mode=star' }, '🔁 重刷收藏'),
  ));

  const list = el('div', {});
  [...State.star].forEach(qid => {
    const q = State.questions.find(x => x.id === qid);
    if (!q) return;
    list.appendChild(el('div', { class: 'search-result-item' },
      el('div', { class: 'quiz-meta' },
        q.year ? el('span', { class: 'tag year' }, `${q.year} ${q.problem}`) : null,
        q.topic ? el('span', { class: 'tag' }, q.topic) : null,
      ),
      el('div', { html: renderInline(q.question.slice(0, 200)) }),
    ));
  });
  app.appendChild(list);
});

/* ============================================================
 * 路由：搜索
 * ============================================================ */
route('/search', (app, params) => {
  app.appendChild(el('div', { class: 'page-title' }, '全局搜索'));
  app.appendChild(el('div', { class: 'page-sub' }, '搜索题目、知识卡片、真题档案'));

  const initial = params.get('q') || '';
  const input = el('input', {
    type: 'text', class: 'search-bar',
    placeholder: '🔍 输入关键词，例如 SPWM、PID、循迹、FFT…',
    value: initial,
  });
  app.appendChild(input);

  const result = el('div', {});
  app.appendChild(result);

  function doSearch(kw) {
    result.innerHTML = '';
    if (!kw || kw.length < 1) {
      result.appendChild(el('div', { class: 'empty' }, '输入关键词开始搜索'));
      return;
    }
    const k = kw.toLowerCase();

    const qHits = State.questions.filter(q =>
      (q.question + (q.options || []).join(' ') + (q.tags || []).join(' ') + q.topic).toLowerCase().includes(k)
    );
    const cHits = State.flashcards.filter(c =>
      (c.front + ' ' + c.back + ' ' + (c.tags || []).join(' ')).toLowerCase().includes(k)
    );
    const pHits = State.problems.filter(p =>
      (p.title + ' ' + (p.tech || '') + ' ' + p.topic + ' ' + p.problem + ' ' + p.year).toLowerCase().includes(k)
    );

    if (qHits.length === 0 && cHits.length === 0 && pHits.length === 0) {
      result.appendChild(el('div', { class: 'empty' }, '没有匹配结果'));
      return;
    }

    if (pHits.length) {
      result.appendChild(el('h2', { style: 'font-size:14px;margin:12px 0 6px;' },
        `📚 真题（${pHits.length}）`));
      pHits.forEach(p => result.appendChild(el('div', { class: 'search-result-item' },
        el('div', { style: 'font-weight:600;' }, `${p.year} ${p.problem} - ${p.title}`),
        el('div', { class: 'page-sub', style: 'margin:4px 0 0;' }, p.tech || ''),
        el('div', { class: 'problem-path' }, p.path),
      )));
    }

    if (qHits.length) {
      result.appendChild(el('h2', { style: 'font-size:14px;margin:18px 0 6px;' },
        `📝 题目（${qHits.length}）`));
      qHits.slice(0, 30).forEach(q => result.appendChild(el('div', {
        class: 'search-result-item',
        onclick: () => {
          State.quizSession = { key: 'one-' + q.id, pool: [q], idx: 0, answered: new Map() };
          location.hash = '#/quiz?mode=star';
        }
      },
        el('div', { class: 'quiz-meta' },
          q.year ? el('span', { class: 'tag year' }, `${q.year} ${q.problem}`) : null,
          el('span', { class: 'tag' }, q.topic),
        ),
        el('div', { html: highlight(q.question.slice(0, 160), k) }),
      )));
    }

    if (cHits.length) {
      result.appendChild(el('h2', { style: 'font-size:14px;margin:18px 0 6px;' },
        `🃏 知识卡片（${cHits.length}）`));
      cHits.forEach(c => result.appendChild(el('div', { class: 'search-result-item' },
        el('div', { style: 'font-weight:600;' }, c.front),
        el('div', { class: 'page-sub', style: 'margin:4px 0 0;', html: highlight(c.back.slice(0, 200), k) }),
      )));
    }
  }

  input.addEventListener('input', (e) => doSearch(e.target.value));
  if (initial) doSearch(initial);
  setTimeout(() => input.focus(), 100);
});

/* ===== Markdown 极简渲染（行内 code / 加粗 / 高亮） ===== */
function renderInline(s) {
  if (!s) return '';
  return escapeHtml(s)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\n/g, '<br>');
}

function highlight(s, kw) {
  const safe = escapeHtml(s);
  if (!kw) return safe;
  const re = new RegExp(escapeHtml(kw).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi');
  return safe.replace(re, m => `<span class="search-hl">${m}</span>`);
}

/* ============================================================
 * 启动
 * ============================================================ */
async function main() {
  try {
    await loadData();
  } catch (e) {
    console.error(e);
    document.getElementById('app').innerHTML =
      `<div class="empty"><div class="empty-ico">📡</div>数据加载失败<br>请用 <code>start.bat</code> 启动，不要 file:// 直开</div>`;
    return;
  }
  updateBadges();
  navigate();

  // 顶栏菜单（移动端）
  document.getElementById('btn-menu').addEventListener('click', openDrawer);
  document.getElementById('drawer-mask').addEventListener('click', closeDrawer);

  // 图片 lightbox：点 .markdown img 放大
  const lightbox = document.getElementById('img-lightbox');
  document.body.addEventListener('click', (e) => {
    const tgt = e.target;
    if (tgt && tgt.tagName === 'IMG' && tgt.closest('.markdown')) {
      lightbox.querySelector('img').src = tgt.src;
      lightbox.classList.add('show');
    }
  });
  lightbox.addEventListener('click', () => lightbox.classList.remove('show'));
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') lightbox.classList.remove('show');
  });

  // 路由切换时更新顶栏标题
  const routeTitles = {
    '/': '仪表盘', '/quiz': '刷题模式', '/cards': '知识卡片',
    '/problems': '真题速查', '/topics': '方向树',
    '/wrong': '错题本', '/star': '收藏夹', '/search': '全局搜索',
    '/problem': '题目', '/contest': '比赛模式', '/material': '作战材料',
  };
  function syncTopbarTitle() {
    const hash = location.hash.replace(/^#/, '') || '/';
    const path = hash.split('?')[0];
    let t = routeTitles[path];
    if (!t) {
      const segs = path.split('/').filter(Boolean);
      while (segs.length > 0) {
        const key = '/' + segs.join('/');
        if (routeTitles[key]) { t = routeTitles[key]; break; }
        segs.pop();
      }
    }
    document.getElementById('topbar-title').textContent = t || '电赛备赛';
  }
  window.addEventListener('hashchange', syncTopbarTitle);
  syncTopbarTitle();

  document.getElementById('btn-reset').addEventListener('click', () => {
    if (confirm('确认清空所有进度？错题本、收藏、打卡都会重置。')) {
      Store.clearAll();
      State.done.clear();
      State.wrong.clear();
      State.star.clear();
      State.streak = { days: 0, last: null };
      persist();
      navigate();
      toast('进度已清空', 'ok');
    }
  });
}

main();
