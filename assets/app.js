/*!
 * app.js - CliJudge++ GUI 单页应用
 * 依赖: markdown.js (window.renderMarkdown)
 */
(function () {
  'use strict';

  // ── DOM / 通用 ─────────────────────────────────────────
  const $ = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  function esc(v) {
    return String(v === null || v === undefined ? '' : v)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function fmtMs(ms) {
    const n = Number(ms);
    if (!isFinite(n) || n < 0) return '-';
    if (n < 1000) return n + ' ms';
    return (n / 1000).toFixed(2) + ' s';
  }

  function fmtMem(kb) {
    const n = Number(kb);
    if (!isFinite(n) || n < 0) return '-';
    if (n < 1024) return n + ' KB';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' MB';
    return (n / 1024 / 1024).toFixed(2) + ' GB';
  }

  function fmtSize(bytes) {
    const n = Number(bytes) || 0;
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    return (n / 1024 / 1024).toFixed(1) + ' MB';
  }

  // ── 状态映射 ──────────────────────────────────────────
  const STATUS_LABEL = {
    AC: 'Accepted', WA: 'Wrong Answer', TLE: 'Time Limit Exceeded',
    MLE: 'Memory Limit Exceeded', RE: 'Runtime Error', CE: 'Compilation Error',
    SE: 'System Error', SK: 'Skipped', PE: 'Presentation Error',
    OLE: 'Output Limit Exceeded', PC: 'Partially Correct', IV: 'Invalid SPJ',
    ST: 'Special Judge TLE', SR: 'Special Judge RE', IE: 'Interactor Error',
    pending: 'Pending', error: 'Error', unknown: 'Unknown'
  };
  const STATUS_CLASS = {
    AC: 'badge-ac', WA: 'badge-wa', TLE: 'badge-tle', MLE: 'badge-tle',
    RE: 'badge-re', CE: 'badge-ce', PE: 'badge-tle', OLE: 'badge-tle',
    SE: 'badge-re', SK: 'badge', PC: 'badge-info', IV: 'badge-ce',
    ST: 'badge-tle', SR: 'badge-re', IE: 'badge-re'
  };

  function statusBadge(status, extraTitle) {
    const ab = String(status || '??');
    const cls = STATUS_CLASS[ab] || '';
    const label = extraTitle || STATUS_LABEL[ab] || ab;
    return '<span class="badge ' + cls + '" title="' + esc(label) + '">' + esc(ab) + '</span>';
  }

  // ── API ───────────────────────────────────────────────
  async function api(path, opts) {
    opts = opts || {};
    const init = { method: opts.method || 'GET', headers: {} };
    if (opts.body !== undefined) {
      init.headers['Content-Type'] = 'application/json';
      init.body = typeof opts.body === 'string' ? opts.body : JSON.stringify(opts.body);
    }
    let res;
    try {
      res = await fetch(path, init);
    } catch (e) {
      throw new Error('无法连接本地服务，请确认 clijudgepp 仍在运行');
    }
    const text = await res.text();
    let data = {};
    if (text) {
      try { data = JSON.parse(text); } catch (e) { data = { error: text }; }
    }
    if (!res.ok) throw new Error(data.error || ('请求失败 (HTTP ' + res.status + ')'));
    return data;
  }

  // ── Toast / Modal ─────────────────────────────────────
  function toast(title, message, type) {
    const root = $('#toast-root');
    const div = document.createElement('div');
    div.className = 'toast ' + (type || '');
    div.innerHTML = '<div class="t-title">' + esc(title) + '</div>' +
                    (message ? '<div>' + esc(message) + '</div>' : '');
    root.appendChild(div);
    setTimeout(function () {
      div.style.transition = 'opacity .3s, transform .3s';
      div.style.opacity = '0';
      div.style.transform = 'translateX(12px)';
      setTimeout(function () { div.remove(); }, 320);
    }, 4200);
  }

  function openModal(opts) {
    const root = $('#modal-root');
    root.innerHTML =
      '<div class="modal-overlay"><div class="modal"' +
      (opts.wide ? ' style="width:min(980px,100%)"' : '') + '>' +
      '<div class="modal-head"><div class="modal-title">' + esc(opts.title) + '</div>' +
      '<button class="modal-close" data-close aria-label="关闭">×</button></div>' +
      '<div class="modal-body">' + opts.body + '</div></div></div>';

    const overlay = root.firstElementChild;
    let closed = false;
    function close() {
      if (closed) return;
      closed = true;
      document.removeEventListener('keydown', onKey);
      root.innerHTML = '';
    }
    function onKey(e) { if (e.key === 'Escape') close(); }
    document.addEventListener('keydown', onKey);
    overlay.addEventListener('mousedown', function (e) { if (e.target === overlay) close(); });
    $$('[data-close]', overlay).forEach(function (b) { b.addEventListener('click', close); });
    if (opts.onMount) opts.onMount(overlay, close);
    return { close: close, overlay: overlay };
  }

  function showFormModal(title, bodyHtml, onSubmit, submitLabel) {
    openModal({
      title: title,
      wide: bodyHtml.length > 2400,
      body: '<form id="modal-form">' + bodyHtml +
            '<div class="form-actions">' +
            '<button type="button" class="btn" data-close>取消</button>' +
            '<button type="submit" class="btn btn-primary">' + esc(submitLabel || '保存') + '</button>' +
            '</div></form>',
      onMount: function (overlay, close) {
        const form = $('#modal-form', overlay);
        const btn = form.querySelector('button[type="submit"]');
        const old = btn.textContent;
        attachMarkdownPreview(form);
        form.addEventListener('submit', async function (e) {
          e.preventDefault();
          btn.disabled = true;
          btn.textContent = '处理中…';
          try {
            await onSubmit(new FormData(form), close);
          } catch (err) {
            toast('操作失败', err.message, 'err');
            btn.disabled = false;
            btn.textContent = old;
          }
        });
      }
    });
  }

  function confirmModal(title, message, onConfirm, label) {
    openModal({
      title: title,
      body: '<p style="color:var(--text-dim);margin-top:0">' + esc(message) + '</p>' +
            '<div class="form-actions">' +
            '<button class="btn" data-close>取消</button>' +
            '<button class="btn btn-danger" id="confirm-yes">' + esc(label || '确认') + '</button>' +
            '</div>',
      onMount: function (overlay, close) {
        const btn = $('#confirm-yes', overlay);
        btn.addEventListener('click', async function () {
          btn.disabled = true;
          btn.textContent = '处理中…';
          try {
            await onConfirm();
            close();
          } catch (err) {
            toast('操作失败', err.message, 'err');
            btn.disabled = false;
            btn.textContent = label || '确认';
          }
        });
      }
    });
  }

  function attachMarkdownPreview(form) {
    const textarea = form.querySelector('textarea[data-md]');
    const btn = form.querySelector('[data-preview]');
    const pane = form.querySelector('[data-preview-pane]');
    if (!textarea || !btn || !pane) return;
    btn.addEventListener('click', function () {
      const visible = pane.style.display !== 'none';
      pane.style.display = visible ? 'none' : 'block';
      if (!visible) $('.md', pane).innerHTML = renderMarkdown(textarea.value);
      btn.textContent = visible ? '预览 Markdown' : '隐藏预览';
    });
  }

  // ── 路由 ──────────────────────────────────────────────
  function routeParts() {
    let h = location.hash || '#/overview';
    h = h.replace(/^#\/?/, '');
    const parts = h.split('/').filter(Boolean).map(function (p) {
      try { return decodeURIComponent(p); } catch (e) { return p; }
    });
    return {
      name: parts[0] || 'overview',
      id: parts[1] ? Number(parts[1]) : null,
      tab: parts[2] || null
    };
  }

  function go(hash) {
    if (location.hash === hash) render();
    else location.hash = hash;
  }

  function setNav(name) {
    $$('.nav-item').forEach(function (a) {
      a.classList.toggle('active', a.getAttribute('data-nav') === name);
    });
  }

  function setBreadcrumb(items) {
    $('#breadcrumb').innerHTML = items.map(function (it, i) {
      const label = i === items.length - 1
        ? esc(it.label)
        : '<span class="crumb-link" data-go="' + esc(it.hash || '#/overview') + '">' + esc(it.label) + '</span>';
      return (i ? '<span class="sep">/</span>' : '') + label;
    }).join('');
    $$('#breadcrumb [data-go]').forEach(function (n) {
      n.addEventListener('click', function () { go(n.getAttribute('data-go')); });
    });
  }

  function tabsHtml(current, items) {
    return '<div class="tabs">' + items.map(function (it) {
      return '<button class="tab ' + (it.key === current ? 'active' : '') +
             '" data-tab="' + it.key + '">' + esc(it.label) + '</button>';
    }).join('') + '</div>';
  }

  function bindTabs(base, id) {
    $$('.tab').forEach(function (b) {
      b.addEventListener('click', function () {
        go(base + '/' + id + '/' + b.getAttribute('data-tab'));
      });
    });
  }

  function pageHead(title, desc, actions) {
    return '<div class="page-head"><div><h1 class="page-title">' + esc(title) + '</h1>' +
           (desc ? '<p class="page-desc">' + esc(desc) + '</p>' : '') +
           '</div>' + (actions ? '<div class="page-actions">' + actions + '</div>' : '') + '</div>';
  }

  function emptyState(text) {
    return '<div class="empty">' + esc(text) + '</div>';
  }

  // ── 主导航 ────────────────────────────────────────────
  async function render() {
    const r = routeParts();
    setNav(r.name);
    const view = $('#view');
    try {
      switch (r.name) {
        case 'problems':
          if (r.id) await viewProblem(r.id, r.tab || 'statement');
          else await viewProblems();
          break;
        case 'articles':
          if (r.id) await viewArticle(r.id);
          else await viewArticles();
          break;
        case 'contests':
          if (r.id) await viewContest(r.id, r.tab || 'overview');
          else await viewContests();
          break;
        case 'submissions':
          await viewSubmissions();
          break;
        default:
          await viewOverview();
      }
    } catch (e) {
      view.innerHTML = pageHead('加载失败', '') +
        '<div class="hint-box">' + esc(e.message) + '</div>' +
        '<button class="btn" id="retry-btn">重试</button>';
      $('#retry-btn').addEventListener('click', render);
    }
    refreshConn();
  }

  async function refreshConn() {
    const dot = $('#conn-dot');
    const text = $('#conn-text');
    const dir = $('#data-dir');
    try {
      const s = await api('/api/status');
      dot.className = 'conn-dot online';
      text.textContent = '服务运行中';
      dir.textContent = s.data_dir;
      dir.title = s.data_dir;
      $('#version').textContent = 'v' + s.version;
    } catch (e) {
      dot.className = 'conn-dot offline';
      text.textContent = '连接断开';
    }
  }

  // ── 概览 ──────────────────────────────────────────────
  async function viewOverview() {
    const s = await api('/api/status');
    setBreadcrumb([{ label: '概览' }]);
    const c = s.counts || {};
    const html =
      pageHead('概览', 'CliJudge++ GUI 与 clijudge CLI 共享同一数据目录') +
      '<div class="cards">' +
        '<div class="card"><div class="card-label">题目</div><div class="card-value accent">' + (c.problems || 0) + '</div></div>' +
        '<div class="card"><div class="card-label">文章</div><div class="card-value green">' + (c.articles || 0) + '</div></div>' +
        '<div class="card"><div class="card-label">比赛</div><div class="card-value purple">' + (c.contests || 0) + '</div></div>' +
        '<div class="card"><div class="card-label">提交记录</div><div class="card-value orange">' + (c.submissions || 0) + '</div></div>' +
      '</div>' +
      (s.lang_configured === false
        ? '<div class="hint-box">显示语言尚未配置（<code>displaylang</code>），GUI 功能不受影响；CLI 首次使用前可执行 <code>clijudge displaylang switch zh-CN</code>。</div>'
        : '') +
      '<div class="split">' +
        '<div class="panel"><div class="panel-head"><span class="panel-title">数据目录</span></div>' +
          '<div class="panel-body"><div class="detail-list">' +
            '<dt>路径</dt><dd>' + esc(s.data_dir) + '</dd>' +
            '<dt>共享方式</dt><dd>GUI 与 CLI 读写同一份 problems / articles / contests / submissions</dd>' +
            '<dt>版本</dt><dd>' + esc(s.version) + '</dd>' +
          '</div></div></div>' +
        '<div class="panel"><div class="panel-head"><span class="panel-title">快速开始</span></div>' +
          '<div class="panel-body">' +
            '<p style="margin-top:0;color:var(--text-dim)">新建题目后，在题目详情的「提交评测」页粘贴源码即可本地沙箱评测；题面与文章均支持 Markdown 渲染。</p>' +
            '<div class="btn-row">' +
              '<button class="btn btn-primary" id="qa-new-problem">新建题目</button>' +
              '<button class="btn" id="qa-problems">浏览题目</button>' +
              '<button class="btn" id="qa-articles">浏览文章</button>' +
            '</div>' +
          '</div></div>' +
      '</div>';
    $('#view').innerHTML = html;

    $('#qa-new-problem').addEventListener('click', function () { openProblemCreate(); });
    $('#qa-problems').addEventListener('click', function () { go('#/problems'); });
    $('#qa-articles').addEventListener('click', function () { go('#/articles'); });
  }

  // ── 题目列表 ──────────────────────────────────────────
  async function viewProblems() {
    setBreadcrumb([{ label: '题目' }]);
    const data = await api('/api/problems');
    const items = data.items || [];
    let html = pageHead('题目', '共 ' + (data.total || 0) + ' 道（点击行进入详情）',
      '<button class="btn btn-primary" id="new-problem">新建题目</button>');
    if (!items.length) {
      html += '<div class="panel">' + emptyState('暂无题目，点击右上角新建') + '</div>';
    } else {
      html += '<div class="panel"><div class="panel-head">' +
              '<span class="panel-title">全部题目</span>' +
              '<input id="problem-search" type="text" placeholder="搜索标题或 ID" style="width:220px">' +
              '</div><div class="table-wrap"><table class="data"><thead><tr>' +
              '<th>ID</th><th>标题</th><th>时间</th><th>内存</th><th>测试点</th></tr></thead><tbody>';
      items.forEach(function (it) {
        const p = it.problem || it;
        const tc = (it.test_cases && it.test_cases.length) || 0;
        html += '<tr class="clickable" data-pid="' + esc(p.id) + '" data-search="' +
                esc((p.id || '') + ' ' + (p.title || '')) + '">' +
                '<td class="id-cell">' + esc(p.id) + '</td>' +
                '<td>' + esc(p.title || '(无标题)') + '</td>' +
                '<td class="mono">' + esc(p.time_limit || 1000) + ' ms</td>' +
                '<td class="mono">' + esc(p.memory_limit || 256) + ' MB</td>' +
                '<td class="mono">' + tc + '</td></tr>';
      });
      html += '</tbody></table></div></div>';
    }
    $('#view').innerHTML = html;

    $('#new-problem').addEventListener('click', openProblemCreate);
    $$('#view tbody tr[data-pid]').forEach(function (tr) {
      tr.addEventListener('click', function () { go('#/problems/' + tr.getAttribute('data-pid')); });
    });
    const search = $('#problem-search');
    if (search) {
      search.addEventListener('input', function () {
        const q = search.value.trim().toLowerCase();
        $$('#view tbody tr[data-pid]').forEach(function (tr) {
          tr.style.display = (!q || tr.getAttribute('data-search').toLowerCase().indexOf(q) >= 0) ? '' : 'none';
        });
      });
    }
  }

  const PROBLEM_TYPES = [
    ['traditional', '传统题'], ['answers_only', '答案提交（输出文件）'],
    ['interaction', '交互题'], ['communication', '通信题'],
    ['communication_exec', '通信题（grader 可执行）']
  ];
  const COMPARE_MODES = [
    ['text_strict', '严格文本'], ['text_line', '逐行比较'], ['text_no_space', '忽略空白'],
    ['float_abs', '浮点绝对误差'], ['float_rel', '浮点相对误差'], ['float_all', '浮点综合误差'],
    ['spj', '自定义 SPJ'], ['spj_lemon', 'LemonLime SPJ'], ['spj_testlib', 'testlib SPJ']
  ];

  function selectOptions(list, cur) {
    return list.map(function (it) {
      return '<option value="' + it[0] + '"' + (it[0] === cur ? ' selected' : '') + '>' + esc(it[1]) + '</option>';
    }).join('');
  }

  function problemFormHtml(p) {
    p = p || {};
    return '<div class="form-grid">' +
      '<div class="field span-2 required"><label>标题</label>' +
      '<input type="text" name="title" value="' + esc(p.title) + '" placeholder="题目名称"></div>' +
      '<div class="field"><label>时间限制 (ms)</label>' +
      '<input type="number" name="time_limit" min="1" value="' + esc(p.time_limit === undefined ? 1000 : p.time_limit) + '"></div>' +
      '<div class="field"><label>内存限制 (MB)</label>' +
      '<input type="number" name="memory_limit" min="1" value="' + esc(p.memory_limit === undefined ? 256 : p.memory_limit) + '"></div>' +
      '<div class="field"><label>题目类型</label><select name="problem_type">' +
      selectOptions(PROBLEM_TYPES, p.problem_type || 'traditional') + '</select></div>' +
      '<div class="field"><label>比对模式</label><select name="compare_mode">' +
      selectOptions(COMPARE_MODES, p.compare_mode || 'text_strict') + '</select></div>' +
      '<div class="field span-2"><label>题面（Markdown）</label>' +
      '<textarea name="description" data-md rows="8" class="mono" placeholder="支持 Markdown：标题 / 列表 / 表格 / 代码块 / 图片 / 链接">' +
      esc(p.description) + '</textarea>' +
      '<button type="button" class="btn btn-sm btn-ghost" data-preview>预览 Markdown</button>' +
      '<div data-preview-pane style="display:none"><div class="preview-pane"><div class="md"></div></div></div></div>' +
      '<div class="field span-2"><label>输入格式（Markdown）</label>' +
      '<textarea name="input_desc" data-md rows="3" class="mono">' + esc(p.input_desc) + '</textarea></div>' +
      '<div class="field span-2"><label>输出格式（Markdown）</label>' +
      '<textarea name="output_desc" data-md rows="3" class="mono">' + esc(p.output_desc) + '</textarea></div>' +
      '<div class="field"><label>样例输入</label>' +
      '<textarea name="sample_input" rows="5" class="mono">' + esc(p.sample_input) + '</textarea></div>' +
      '<div class="field"><label>样例输出</label>' +
      '<textarea name="sample_output" rows="5" class="mono">' + esc(p.sample_output) + '</textarea></div>' +
      '<div class="field span-2"><label>提示（Markdown）</label>' +
      '<textarea name="hint" data-md rows="4" class="mono">' + esc(p.hint) + '</textarea></div>' +
      '</div>';
  }

  function problemPayload(fd) {
    return {
      title: String(fd.get('title') || '').trim(),
      time_limit: Number(fd.get('time_limit')) || 1000,
      memory_limit: Number(fd.get('memory_limit')) || 256,
      problem_type: String(fd.get('problem_type') || 'traditional'),
      compare_mode: String(fd.get('compare_mode') || 'text_strict'),
      description: String(fd.get('description') || ''),
      input_desc: String(fd.get('input_desc') || ''),
      output_desc: String(fd.get('output_desc') || ''),
      sample_input: String(fd.get('sample_input') || ''),
      sample_output: String(fd.get('sample_output') || ''),
      hint: String(fd.get('hint') || '')
    };
  }

  function openProblemCreate() {
    showFormModal('新建题目', problemFormHtml({}), async function (fd, close) {
      const payload = problemPayload(fd);
      if (!payload.title) throw new Error('标题不能为空');
      const r = await api('/api/problems', { method: 'POST', body: payload });
      close();
      toast('题目已创建', '#' + r.id + ' ' + payload.title, 'ok');
      go('#/problems/' + r.id);
    }, '创建');
  }

  function openProblemEdit(id, p) {
    showFormModal('编辑题目 #' + id, problemFormHtml(p), async function (fd, close) {
      const payload = problemPayload(fd);
      if (!payload.title) throw new Error('标题不能为空');
      await api('/api/problems/' + id, { method: 'PUT', body: payload });
      close();
      toast('已保存', '题目 #' + id, 'ok');
      render();
    });
  }

  // ── 题目详情 ──────────────────────────────────────────
  async function viewProblem(id, tab) {
    const p = await api('/api/problems/' + id);
    if (!p || !p.problem) throw new Error('题目不存在或数据损坏');
    const prob = p.problem;
    const testCases = (p.test_cases || []).slice().sort(function (a, b) {
      return (Number(a.sort_order) || 0) - (Number(b.sort_order) || 0);
    });
    currentProblemCache = testCases;

    setBreadcrumb([
      { label: '题目', hash: '#/problems' },
      { label: '#' + id + ' ' + (prob.title || '') }
    ]);

    const totalScore = testCases.reduce(function (a, t) { return a + (Number(t.score) || 0); }, 0);
    const head = pageHead(prob.title || ('题目 #' + id),
      '时间 ' + (prob.time_limit || 1000) + ' ms · 内存 ' + (prob.memory_limit || 256) + ' MB · ' +
      testCases.length + ' 个测试点 · 总分 ' + totalScore,
      '<button class="btn" id="edit-problem">编辑题目</button>' +
      '<button class="btn btn-danger" id="delete-problem">删除</button>');

    let body = '';
    if (tab === 'statement') body = renderStatement(prob);
    else if (tab === 'testcases') body = renderTestCases(testCases);
    else if (tab === 'submit') body = renderSubmitTab(id);
    else if (tab === 'submissions') body = await renderProblemSubmissions(id);
    else body = renderStatement(prob);

    $('#view').innerHTML = head + tabsHtml(tab, [
      { key: 'statement', label: '题面' },
      { key: 'testcases', label: '测试数据 (' + testCases.length + ')' },
      { key: 'submit', label: '提交评测' },
      { key: 'submissions', label: '提交记录' }
    ]) + body;

    bindTabs('#/problems', id);
    $('#edit-problem').addEventListener('click', function () { openProblemEdit(id, prob); });
    $('#delete-problem').addEventListener('click', function () {
      confirmModal('删除题目', '确定删除题目 #' + id + '「' + (prob.title || '') + '」？此操作不可撤销。', async function () {
        await api('/api/problems/' + id, { method: 'DELETE' });
        toast('题目已删除', '', 'ok');
        go('#/problems');
      }, '删除');
    });

    if (tab === 'testcases') bindTestCases(id);
    if (tab === 'submit') bindSubmitTab(id);
    if (tab === 'submissions') bindSubmissionRows();
  }

  function renderStatement(p) {
    let html = '<div class="panel problem-card">';
    html += '<div class="problem-meta" style="margin-bottom:18px">' +
      '<span class="badge badge-info">' + esc(p.problem_type || 'traditional') + '</span>' +
      '<span class="badge">' + esc(p.compare_mode || 'text_strict') + '</span>' +
      (p.is_hidden ? '<span class="badge">隐藏</span>' : '') + '</div>';

    html += '<div class="md">' + renderMarkdown(p.description || '*暂无题面*') + '</div>';

    if (p.input_desc) {
      html += '<h3 style="margin-top:26px">输入格式</h3><div class="md">' + renderMarkdown(p.input_desc) + '</div>';
    }
    if (p.output_desc) {
      html += '<h3 style="margin-top:26px">输出格式</h3><div class="md">' + renderMarkdown(p.output_desc) + '</div>';
    }
    if (p.sample_input || p.sample_output) {
      html += '<h3 style="margin-top:26px">样例</h3><div class="split">' +
        '<div class="sample-block"><div class="sample-head">样例输入</div>' +
        '<pre class="sample-body">' + esc(p.sample_input || '(空)') + '</pre></div>' +
        '<div class="sample-block"><div class="sample-head">样例输出</div>' +
        '<pre class="sample-body">' + esc(p.sample_output || '(空)') + '</pre></div>' +
        '</div>';
    }
    if (p.hint) {
      html += '<h3 style="margin-top:26px">提示</h3><div class="md">' + renderMarkdown(p.hint) + '</div>';
    }
    html += '</div>';
    return html;
  }

  function renderTestCases(testCases) {
    if (!testCases.length) {
      return '<div class="hint-box">该题目还没有测试点，添加后才能评测。</div>' +
        '<button class="btn btn-primary" id="add-testcase">添加测试点</button>';
    }
    return '<div class="page-actions" style="margin-bottom:16px">' +
      '<button class="btn btn-primary" id="add-testcase">添加测试点</button></div>' +
      '<div class="tc-grid">' + testCases.map(function (t) {
        return '<div class="tc-card" data-tc="' + esc(t.id) + '">' +
          '<div class="tc-head"><span class="mono">#' + esc(t.id) + '</span>' +
          '<span class="badge">' + esc(t.score) + ' 分</span></div>' +
          '<div class="tc-body">' +
          '<div class="tc-label">输入</div><pre>' + esc(t.input_data || (t.input_file ? '文件: ' + t.input_file : '(空)')) + '</pre>' +
          '<div class="tc-label">输出</div><pre>' + esc(t.output_data || (t.output_file ? '文件: ' + t.output_file : '(空)')) + '</pre>' +
          '<div class="tc-label">子任务 #' + esc(t.subtask_id || 1) + '</div>' +
          '</div>' +
          '<div class="tc-actions">' +
          '<button class="btn btn-sm" data-edit-tc>编辑</button>' +
          '<button class="btn btn-sm btn-danger" data-del-tc>删除</button>' +
          '</div></div>';
      }).join('') + '</div>';
  }

  function bindTestCases(problemId) {
    const p = currentProblemCache;
    $('#add-testcase').addEventListener('click', function () { openTestCaseModal(problemId, null); });
    $$('.tc-card[data-tc]').forEach(function (card) {
      const tcId = Number(card.getAttribute('data-tc'));
      const tc = (p || []).find(function (t) { return Number(t.id) === tcId; });
      card.querySelector('[data-edit-tc]').addEventListener('click', function () {
        openTestCaseModal(problemId, tc);
      });
      card.querySelector('[data-del-tc]').addEventListener('click', function () {
        confirmModal('删除测试点', '确定删除测试点 #' + tcId + '？', async function () {
          await api('/api/problems/' + problemId + '/testcases/' + tcId, { method: 'DELETE' });
          toast('测试点已删除', '', 'ok');
          render();
        }, '删除');
      });
    });
  }

  let currentProblemCache = null;

  function openTestCaseModal(problemId, tc) {
    const t = tc || {};
    showFormModal(tc ? ('编辑测试点 #' + tc.id) : '添加测试点',
      '<div class="form-grid">' +
      '<div class="field span-2"><label>输入数据</label>' +
      '<textarea name="input_data" rows="7" class="mono">' + esc(t.input_data) + '</textarea></div>' +
      '<div class="field span-2"><label>输出数据</label>' +
      '<textarea name="output_data" rows="7" class="mono">' + esc(t.output_data) + '</textarea></div>' +
      '<div class="field"><label>分值</label><input type="number" name="score" min="0" value="' +
      esc(t.score === undefined ? 25 : t.score) + '"></div>' +
      '<div class="field"><label>子任务编号</label><input type="number" name="subtask_id" min="1" value="' +
      esc(t.subtask_id === undefined ? 1 : t.subtask_id) + '"></div>' +
      '<div class="field"><label>时间限制 (ms，-1 继承)</label><input type="number" name="time_limit" value="' +
      esc(t.time_limit === undefined ? -1 : t.time_limit) + '"></div>' +
      '<div class="field"><label>内存限制 (MB，-1 继承)</label><input type="number" name="memory_limit" value="' +
      esc(t.memory_limit === undefined ? -1 : t.memory_limit) + '"></div>' +
      '</div>',
      async function (fd, close) {
        const payload = {
          input_data: String(fd.get('input_data') || ''),
          output_data: String(fd.get('output_data') || ''),
          score: Number(fd.get('score')) || 0,
          subtask_id: Number(fd.get('subtask_id')) || 1,
          time_limit: Number(fd.get('time_limit')) || -1,
          memory_limit: Number(fd.get('memory_limit')) || -1
        };
        if (tc) {
          await api('/api/problems/' + problemId + '/testcases/' + tc.id, { method: 'PUT', body: payload });
          toast('测试点已更新', '', 'ok');
        } else {
          await api('/api/problems/' + problemId + '/testcases', { method: 'POST', body: payload });
          toast('测试点已添加', '', 'ok');
        }
        close();
        go('#/problems/' + problemId + '/testcases');
      }, tc ? '保存' : '添加');
  }

  const LANGS = [
    ['.cpp', 'C++17'], ['.c', 'C'], ['.cc', 'C++ (cc)'], ['.py', 'Python 3'],
    ['.java', 'Java'], ['.rs', 'Rust'], ['.go', 'Go'], ['.js', 'JavaScript']
  ];
  const CODE_TEMPLATE = {
    '.cpp': '#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    \n    return 0;\n}\n',
    '.cc': '#include <bits/stdc++.h>\nusing namespace std;\n\nint main() {\n    \n    return 0;\n}\n',
    '.c': '#include <stdio.h>\n\nint main(void) {\n    \n    return 0;\n}\n',
    '.py': 'import sys\n\ndef main():\n    pass\n\nif __name__ == "__main__":\n    main()\n',
    '.java': 'import java.util.*;\n\npublic class Main {\n    public static void main(String[] args) {\n        \n    }\n}\n',
    '.rs': 'use std::io::{self, Read};\n\nfn main() {\n    \n}\n',
    '.go': 'package main\n\nimport "fmt"\n\nfunc main() {\n    \n}\n',
    '.js': 'const data = require("fs").readFileSync(0, "utf8");\n\n'
  };

  function langSelectHtml(id) {
    return '<select id="' + id + '">' + LANGS.map(function (l) {
      return '<option value="' + l[0] + '">' + esc(l[1]) + '</option>';
    }).join('') + '</select>';
  }

  function renderSubmitTab(id) {
    return '<div class="hint-box">提交后将在本地沙箱编译并评测，时间/内存限制取自题目配置。</div>' +
      '<div class="editor-shell">' +
      '<div class="editor-bar"><div class="left">' + langSelectHtml('lang-select') +
      '<input id="submit-user" type="text" placeholder="选手名（可选）" style="width:170px"></div>' +
      '<div class="right"><button class="btn btn-primary" id="submit-code">提交评测</button></div></div>' +
      '<textarea id="code-editor" spellcheck="false">' + esc(CODE_TEMPLATE['.cpp']) + '</textarea>' +
      '</div><div id="judge-result"></div>';
  }

  function bindSubmitTab(id) {
    const langSel = $('#lang-select');
    const editor = $('#code-editor');
    langSel.addEventListener('change', function () {
      if (!editor.value.trim() || editor.value === CODE_TEMPLATE['.cpp']) {
        editor.value = CODE_TEMPLATE[langSel.value] || CODE_TEMPLATE['.cpp'];
      }
    });

    $('#submit-code').addEventListener('click', async function () {
      const btn = this;
      const code = editor.value;
      if (!code.trim()) { toast('提示', '请先输入代码', 'err'); return; }
      btn.disabled = true;
      btn.textContent = '评测中…';
      $('#judge-result').innerHTML = '<div class="loading">正在编译并运行测试点…</div>';
      try {
        const r = await api('/api/problems/' + id + '/submit', {
          method: 'POST',
          body: { code: code, ext: langSel.value, username: $('#submit-user').value.trim() }
        });
        renderJudgeResult(r);
        toast('评测完成', '状态 ' + (r.status || '?') + ' · 得分 ' + (r.score || 0), 'ok');
      } catch (e) {
        $('#judge-result').innerHTML = '<div class="hint-box">评测失败：' + esc(e.message) + '</div>';
        toast('评测失败', e.message, 'err');
      } finally {
        btn.disabled = false;
        btn.textContent = '提交评测';
      }
    });
  }

  function renderJudgeResult(r, targetId) {
    const d = r.judge_detail || {};
    const status = d.status_abbr || r.status || '??';
    const score = d.total_score !== undefined ? d.total_score : (r.score || 0);
    const maxScore = d.max_score !== undefined ? d.max_score : '';
    const time = d.total_time_ms !== undefined ? d.total_time_ms : r.time_used;
    const mem = d.max_memory_kb !== undefined ? d.max_memory_kb : r.memory_used;

    let html = '<div class="result-panel"><div class="result-head">' +
      '<div style="display:flex;align-items:center;gap:10px">' +
      '<span style="font-weight:600">评测结果</span>' + statusBadge(status) + '</div>' +
      (r.submission_id !== undefined
        ? '<span class="badge badge-info">提交记录 #' + esc(r.submission_id) + '</span>' : '') +
      '</div><div class="result-grid">' +
      '<div class="result-cell"><div class="k">得分</div><div class="v">' + esc(score) +
      (maxScore !== '' ? ' / ' + esc(maxScore) : '') + '</div></div>' +
      '<div class="result-cell"><div class="k">总用时</div><div class="v">' + esc(fmtMs(time)) + '</div></div>' +
      '<div class="result-cell"><div class="k">峰值内存</div><div class="v">' + esc(fmtMem(mem)) + '</div></div>' +
      '</div>';

    if (d.compile_error) {
      html += '<div class="panel-body"><div class="tc-label">编译错误</div>' +
        '<pre class="sample-body" style="border-radius:8px;border:1px solid var(--border-soft)">' +
        esc(d.compile_error) + '</pre></div>';
    }

    const cases = [];
    (d.subtasks || []).forEach(function (st) {
      (st.test_cases || []).forEach(function (tc) { cases.push(tc); });
    });
    if (cases.length) {
      html += '<div class="case-list">' + cases.map(function (tc) {
        return '<div class="case-item">' +
          '<span class="case-id">#' + esc(tc.id) + '</span>' +
          statusBadge(tc.status_abbr || tc.status) +
          '<span class="badge">' + esc(tc.score) + '/' + esc(tc.max_score) + '</span>' +
          '<span class="case-msg">' + esc(tc.time_ms) + ' ms · ' + esc(fmtMem(tc.memory_kb)) +
          (tc.message ? ' · ' + esc(tc.message) : '') + '</span>' +
          '</div>';
      }).join('') + '</div>';
    }
    html += '</div>';
    $('#' + (targetId || 'judge-result')).innerHTML = html;
  }

  async function renderProblemSubmissions(problemId) {
    const data = await api('/api/submissions');
    const items = (data.items || []).filter(function (s) { return Number(s.problem_id) === Number(problemId); });
    if (!items.length) return '<div class="panel">' + emptyState('本题暂无提交记录') + '</div>';
    return '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
      '<th>ID</th><th>状态</th><th>得分</th><th>用时</th><th>内存</th><th>选手</th><th>时间</th></tr></thead><tbody>' +
      items.map(function (s) {
        return '<tr class="clickable" data-sid="' + esc(s.id) + '">' +
          '<td class="id-cell">' + esc(s.id) + '</td>' +
          '<td>' + statusBadge(s.status) + '</td>' +
          '<td class="mono">' + esc(s.score) + '</td>' +
          '<td class="mono">' + esc(fmtMs(s.time_used)) + '</td>' +
          '<td class="mono">' + esc(fmtMem(s.memory_used)) + '</td>' +
          '<td>' + esc(s.username || '-') + '</td>' +
          '<td class="time-cell">' + esc(s.submitted_at || '-') + '</td></tr>';
      }).join('') + '</tbody></table></div></div>';
  }

  function bindSubmissionRows() {
    $$('#view tbody tr[data-sid]').forEach(function (tr) {
      tr.addEventListener('click', function () { openSubmissionDetail(Number(tr.getAttribute('data-sid'))); });
    });
  }

  // ── 文章 ──────────────────────────────────────────────
  async function viewArticles() {
    setBreadcrumb([{ label: '文章' }]);
    const data = await api('/api/articles');
    const items = data.items || [];
    let html = pageHead('文章', '共 ' + (data.total || 0) + ' 篇 · 正文支持 Markdown',
      '<button class="btn btn-primary" id="new-article">新建文章</button>');
    if (!items.length) {
      html += '<div class="panel">' + emptyState('暂无文章') + '</div>';
    } else {
      html += '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
        '<th>ID</th><th>标题</th><th>大小</th><th>更新时间</th></tr></thead><tbody>' +
        items.map(function (a) {
          return '<tr class="clickable" data-aid="' + esc(a.id) + '">' +
            '<td class="id-cell">' + esc(a.id) + '</td>' +
            '<td>' + esc(a.title || '(无标题)') + '</td>' +
            '<td class="mono">' + esc(fmtSize(a.size)) + '</td>' +
            '<td class="time-cell">' + esc(a.updated_at || a.created_at || '-') + '</td></tr>';
        }).join('') + '</tbody></table></div></div>';
    }
    $('#view').innerHTML = html;
    $('#new-article').addEventListener('click', openArticleCreate);
    $$('#view tbody tr[data-aid]').forEach(function (tr) {
      tr.addEventListener('click', function () { go('#/articles/' + tr.getAttribute('data-aid')); });
    });
  }

  function openArticleCreate() {
    showFormModal('新建文章',
      '<div class="form-grid">' +
      '<div class="field span-2 required"><label>标题</label>' +
      '<input type="text" name="title" placeholder="文章标题"></div>' +
      '<div class="field span-2"><label>正文（Markdown）</label>' +
      '<textarea name="content" data-md rows="14" class="mono" placeholder="支持 Markdown 语法"></textarea>' +
      '<button type="button" class="btn btn-sm btn-ghost" data-preview>预览 Markdown</button>' +
      '<div data-preview-pane style="display:none"><div class="preview-pane"><div class="md"></div></div></div></div>' +
      '</div>',
      async function (fd, close) {
        const title = String(fd.get('title') || '').trim();
        if (!title) throw new Error('标题不能为空');
        const r = await api('/api/articles', {
          method: 'POST',
          body: { title: title, content: String(fd.get('content') || '') }
        });
        close();
        toast('文章已创建', '#' + r.id, 'ok');
        go('#/articles/' + r.id);
      }, '创建');
  }

  async function viewArticle(id) {
    const a = await api('/api/articles/' + id);
    setBreadcrumb([
      { label: '文章', hash: '#/articles' },
      { label: '#' + id + ' ' + (a.title || '') }
    ]);
    const html = pageHead(a.title || ('文章 #' + id),
      (a.updated_at || a.created_at || '') + (a.author ? ' · ' + a.author : ''),
      '<button class="btn btn-danger" id="delete-article">删除文章</button>') +
      '<div class="panel problem-card"><div class="md">' +
      renderMarkdown(a.content || '*（空文章）*') + '</div></div>';
    $('#view').innerHTML = html;
    $('#delete-article').addEventListener('click', function () {
      confirmModal('删除文章', '确定删除文章 #' + id + '「' + (a.title || '') + '」？', async function () {
        await api('/api/articles/' + id, { method: 'DELETE' });
        toast('文章已删除', '', 'ok');
        go('#/articles');
      }, '删除');
    });
  }

  // ── 比赛 ──────────────────────────────────────────────
  async function viewContests() {
    setBreadcrumb([{ label: '比赛' }]);
    const data = await api('/api/contests');
    const items = data.items || [];
    let html = pageHead('比赛', '共 ' + (data.total || 0) + ' 场',
      '<button class="btn btn-primary" id="new-contest">新建比赛</button>');
    if (!items.length) {
      html += '<div class="panel">' + emptyState('暂无比赛') + '</div>';
    } else {
      html += '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
        '<th>ID</th><th>标题</th><th>开始</th><th>结束</th><th>题目数</th></tr></thead><tbody>' +
        items.map(function (c) {
          return '<tr class="clickable" data-cid="' + esc(c.id) + '">' +
            '<td class="id-cell">' + esc(c.id) + '</td>' +
            '<td>' + esc(c.title || '(无标题)') + '</td>' +
            '<td class="time-cell">' + esc(c.start_time || '-') + '</td>' +
            '<td class="time-cell">' + esc(c.end_time || '-') + '</td>' +
            '<td class="mono">' + esc(c.problem_count || 0) + '</td></tr>';
        }).join('') + '</tbody></table></div></div>';
    }
    $('#view').innerHTML = html;
    $('#new-contest').addEventListener('click', openContestCreate);
    $$('#view tbody tr[data-cid]').forEach(function (tr) {
      tr.addEventListener('click', function () { go('#/contests/' + tr.getAttribute('data-cid')); });
    });
  }

  function toDateTimeValue(s) {
    if (!s) return '';
    return String(s).replace(' ', 'T').slice(0, 16);
  }

  function openContestCreate(prefill) {
    prefill = prefill || {};
    const pids = (prefill.problem_ids || []).join(', ');
    showFormModal('新建比赛',
      '<div class="form-grid">' +
      '<div class="field span-2 required"><label>标题</label>' +
      '<input type="text" name="title" placeholder="比赛标题"></div>' +
      '<div class="field"><label>开始时间</label>' +
      '<input type="datetime-local" name="start_time" value="' + esc(toDateTimeValue(prefill.start_time)) + '"></div>' +
      '<div class="field"><label>结束时间</label>' +
      '<input type="datetime-local" name="end_time" value="' + esc(toDateTimeValue(prefill.end_time)) + '"></div>' +
      '<div class="field span-2"><label>题目 ID 列表</label>' +
      '<input type="text" name="problem_ids" class="mono" placeholder="例如 1, 2, 3" value="' + esc(pids) + '">' +
      '<span class="hint">按顺序对应比赛题号 A、B、C…</span></div>' +
      '</div>',
      async function (fd, close) {
        const title = String(fd.get('title') || '').trim();
        if (!title) throw new Error('标题不能为空');
        const ids = String(fd.get('problem_ids') || '').split(/[,，\s]+/).filter(Boolean).map(Number);
        if (ids.some(function (n) { return !Number.isFinite(n) || n <= 0; })) {
          throw new Error('题目 ID 必须是正整数，用逗号分隔');
        }
        const norm = function (v) {
          if (!v) return '';
          return String(v).replace('T', ' ') + (String(v).length === 16 ? ':00' : '');
        };
        const r = await api('/api/contests', {
          method: 'POST',
          body: {
            title: title,
            start_time: norm(fd.get('start_time')),
            end_time: norm(fd.get('end_time')),
            problem_ids: ids
          }
        });
        close();
        toast('比赛已创建', '#' + r.id, 'ok');
        go('#/contests/' + r.id);
      }, '创建');
  }

  async function viewContest(id, tab) {
    const c = await api('/api/contests/' + id);
    if (!c || !c.id) throw new Error('比赛不存在');
    setBreadcrumb([
      { label: '比赛', hash: '#/contests' },
      { label: '#' + id + ' ' + (c.title || '') }
    ]);

    let body = '';
    if (tab === 'leaderboard') body = await renderLeaderboard(id);
    else if (tab === 'problems') body = renderContestProblems(id, c.problems || []);
    else body = renderContestOverview(c);

    $('#view').innerHTML =
      pageHead(c.title || ('比赛 #' + id), '开始 ' + (c.start_time || '-') + ' · 结束 ' + (c.end_time || '-'),
        '<button class="btn" id="contest-report">导出 HTML 战报</button>' +
        '<button class="btn btn-danger" id="delete-contest">删除</button>') +
      tabsHtml(tab, [
        { key: 'overview', label: '概览' },
        { key: 'leaderboard', label: '排行榜' },
        { key: 'problems', label: '题目' }
      ]) + body;

    bindTabs('#/contests', id);
    $('#contest-report').addEventListener('click', function () {
      window.open('/api/contests/' + id + '/report', '_blank');
    });
    $('#delete-contest').addEventListener('click', function () {
      confirmModal('删除比赛', '确定删除比赛 #' + id + '「' + (c.title || '') + '」？', async function () {
        await api('/api/contests/' + id, { method: 'DELETE' });
        toast('比赛已删除', '', 'ok');
        go('#/contests');
      }, '删除');
    });

    if (tab === 'problems') bindContestProblems(id);
  }

  function renderContestOverview(c) {
    const probs = c.problems || [];
    return '<div class="split"><div class="panel"><div class="panel-head"><span class="panel-title">比赛信息</span></div>' +
      '<div class="panel-body"><div class="detail-list">' +
      '<dt>开始时间</dt><dd>' + esc(c.start_time || '-') + '</dd>' +
      '<dt>结束时间</dt><dd>' + esc(c.end_time || '-') + '</dd>' +
      '<dt>题目数量</dt><dd>' + probs.length + '</dd>' +
      '<dt>创建时间</dt><dd>' + esc(c.created_at || '-') + '</dd>' +
      '</div></div></div>' +
      '<div class="panel"><div class="panel-head"><span class="panel-title">题目清单</span></div>' +
      '<div class="panel-body">' +
      (probs.length ? '<div class="btn-row">' + probs.map(function (p) {
        return '<a class="btn btn-sm" href="#/problems/' + esc(p.problem_id) + '">' +
          String.fromCharCode(64 + Number(p.index)) + '. ' + esc(p.title || ('题目 #' + p.problem_id)) + '</a>';
      }).join('') + '</div>' : '<span style="color:var(--muted)">该比赛暂无题目</span>') +
      '</div></div></div>';
  }

  async function renderLeaderboard(id) {
    const data = await api('/api/contests/' + id + '/leaderboard');
    const items = data.items || [];
    if (!items.length) return '<div class="panel">' + emptyState('暂无排名（还没有提交记录）') + '</div>';
    return '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
      '<th>排名</th><th>选手</th><th>总分</th><th>通过题数</th><th>提交次数</th></tr></thead><tbody>' +
      items.map(function (it, i) {
        return '<tr><td class="id-cell">' + (i + 1) + '</td>' +
          '<td>' + esc(it.username || 'unknown') + '</td>' +
          '<td class="mono">' + esc(it.score || 0) + '</td>' +
          '<td class="mono">' + esc(it.accepted || 0) + '</td>' +
          '<td class="mono">' + esc(it.total || 0) + '</td></tr>';
      }).join('') + '</tbody></table></div></div>';
  }

  function renderContestProblems(id, problems) {
    if (!problems.length) return '<div class="panel">' + emptyState('该比赛暂无题目') + '</div>';
    return '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
      '<th>题号</th><th>题目</th><th>操作</th></tr></thead><tbody>' +
      problems.map(function (p) {
        return '<tr><td class="id-cell">' + String.fromCharCode(64 + Number(p.index)) + '</td>' +
          '<td><a href="#/problems/' + esc(p.problem_id) + '">' + esc(p.title || ('题目 #' + p.problem_id)) + '</a>' +
          ' <span class="badge">#' + esc(p.problem_id) + '</span></td>' +
          '<td><div class="btn-row">' +
          '<button class="btn btn-sm btn-primary" data-csubmit="' + esc(p.index) + '" data-ctitle="' +
          esc(p.title || '') + '">提交</button>' +
          '<button class="btn btn-sm" data-csubs="' + esc(p.index) + '">提交记录</button>' +
          '</div></td></tr>';
      }).join('') + '</tbody></table></div></div>';
  }

  function bindContestProblems(id) {
    $$('[data-csubmit]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        openContestSubmit(id, Number(btn.getAttribute('data-csubmit')), btn.getAttribute('data-ctitle'));
      });
    });
    $$('[data-csubs]').forEach(function (btn) {
      btn.addEventListener('click', async function () {
        const idx = Number(btn.getAttribute('data-csubs'));
        try {
          const data = await api('/api/contests/' + id + '/problems/' + idx + '/submissions');
          const items = data.items || [];
          openModal({
            title: '题 ' + String.fromCharCode(64 + idx) + ' 提交记录',
            wide: true,
            body: items.length ? '<div class="table-wrap"><table class="data"><thead><tr>' +
              '<th>选手</th><th>结果</th><th>得分</th><th>用时</th><th>内存</th><th>时间</th></tr></thead><tbody>' +
              items.map(function (s, i) {
                return '<tr data-cidx="' + i + '" class="clickable">' +
                  '<td>' + esc(s.username || 'unknown') + '</td>' +
                  '<td>' + statusBadge(s.result) + '</td>' +
                  '<td class="mono">' + esc(s.score || 0) + '</td>' +
                  '<td class="mono">' + esc(fmtMs(s.time_used)) + '</td>' +
                  '<td class="mono">' + esc(fmtMem(s.memory_used)) + '</td>' +
                  '<td class="time-cell">' + esc(s.submitted_at || '-') + '</td></tr>';
              }).join('') + '</tbody></table></div>'
              : emptyState('暂无提交记录'),
            onMount: function (overlay) {
              $$('tr[data-cidx]', overlay).forEach(function (tr) {
                tr.addEventListener('click', function () {
                  const s = items[Number(tr.getAttribute('data-cidx'))];
                  showContestSubmission(s);
                });
              });
            }
          });
        } catch (e) {
          toast('加载失败', e.message, 'err');
        }
      });
    });
  }

  function showContestSubmission(s) {
    openModal({
      title: '提交详情 · ' + (s.username || 'unknown'),
      wide: true,
      body: '<div class="detail-list" style="margin-bottom:16px">' +
        '<dt>状态</dt><dd>' + statusBadge(s.result) + '</dd>' +
        '<dt>得分</dt><dd>' + esc(s.score || 0) + '</dd>' +
        '<dt>提交时间</dt><dd>' + esc(s.submitted_at || '-') + '</dd>' +
        '<dt>源文件</dt><dd>' + esc(s.file_path || '-') + '</dd>' +
        '</div>' + judgeDetailHtml(s.judge_detail, s.result, s.score, s.time_used, s.memory_used)
    });
  }

  function judgeDetailHtml(d, status, score, time, mem) {
    d = d || {};
    const cases = [];
    (d.subtasks || []).forEach(function (st) {
      (st.test_cases || []).forEach(function (tc) { cases.push(tc); });
    });
    let html = '<div class="result-panel"><div class="result-grid">' +
      '<div class="result-cell"><div class="k">状态</div><div class="v">' + statusBadge(d.status_abbr || status) + '</div></div>' +
      '<div class="result-cell"><div class="k">得分</div><div class="v">' + esc(d.total_score !== undefined ? d.total_score : (score || 0)) +
      (d.max_score !== undefined ? ' / ' + esc(d.max_score) : '') + '</div></div>' +
      '<div class="result-cell"><div class="k">用时</div><div class="v">' + esc(fmtMs(d.total_time_ms !== undefined ? d.total_time_ms : time)) + '</div></div>' +
      '<div class="result-cell"><div class="k">内存</div><div class="v">' + esc(fmtMem(d.max_memory_kb !== undefined ? d.max_memory_kb : mem)) + '</div></div>' +
      '</div>';
    if (d.compile_error) {
      html += '<div class="panel-body"><div class="tc-label">编译错误</div>' +
        '<pre class="sample-body" style="border-radius:8px;border:1px solid var(--border-soft)">' + esc(d.compile_error) + '</pre></div>';
    }
    if (cases.length) {
      html += '<div class="case-list">' + cases.map(function (tc) {
        return '<div class="case-item"><span class="case-id">#' + esc(tc.id) + '</span>' +
          statusBadge(tc.status_abbr || tc.status) +
          '<span class="badge">' + esc(tc.score) + '/' + esc(tc.max_score) + '</span>' +
          '<span class="case-msg">' + esc(tc.time_ms) + ' ms · ' + esc(fmtMem(tc.memory_kb)) +
          (tc.message ? ' · ' + esc(tc.message) : '') + '</span></div>';
      }).join('') + '</div>';
    }
    html += '</div>';
    return html;
  }

  function openContestSubmit(cid, idx, title) {
    const letter = String.fromCharCode(64 + idx);
    openModal({
      title: '提交 · ' + letter + ' ' + (title || ''),
      wide: true,
      body: '<div class="editor-bar" style="border:1px solid var(--border);border-radius:12px 12px 0 0">' +
        '<div class="left">' + langSelectHtml('c-lang') +
        '<input id="c-user" type="text" placeholder="选手名（默认 unknown）" style="width:200px"></div>' +
        '<div class="right"><button class="btn btn-primary" id="c-submit">提交评测</button></div></div>' +
        '<div class="editor-shell" style="border-radius:0 0 12px 12px"><textarea id="c-code" spellcheck="false">' +
        esc(CODE_TEMPLATE['.cpp']) + '</textarea></div><div id="c-result"></div>',
      onMount: function (overlay) {
        const langSel = $('#c-lang', overlay);
        const editor = $('#c-code', overlay);
        langSel.addEventListener('change', function () {
          if (!editor.value.trim() || editor.value === CODE_TEMPLATE['.cpp']) {
            editor.value = CODE_TEMPLATE[langSel.value] || CODE_TEMPLATE['.cpp'];
          }
        });
        $('#c-submit', overlay).addEventListener('click', async function () {
          const btn = this;
          if (!editor.value.trim()) { toast('提示', '请先输入代码', 'err'); return; }
          btn.disabled = true;
          btn.textContent = '评测中…';
          $('#c-result', overlay).innerHTML = '<div class="loading">正在评测…</div>';
          try {
            const r = await api('/api/contests/' + cid + '/problems/' + idx + '/submit', {
              method: 'POST',
              body: { code: editor.value, ext: langSel.value, username: $('#c-user', overlay).value.trim() }
            });
            const s = r.submission || {};
            $('#c-result', overlay).innerHTML = judgeDetailHtml(
              s.judge_detail, s.result, s.score, s.time_used, s.memory_used);
            toast('评测完成', '结果 ' + (s.result || '?'), 'ok');
          } catch (e) {
            $('#c-result', overlay).innerHTML = '<div class="hint-box">评测失败：' + esc(e.message) + '</div>';
            toast('评测失败', e.message, 'err');
          } finally {
            btn.disabled = false;
            btn.textContent = '提交评测';
          }
        });
      }
    });
  }

  // ── 全局提交记录 ──────────────────────────────────────
  async function viewSubmissions() {
    setBreadcrumb([{ label: '提交记录' }]);
    const data = await api('/api/submissions');
    const items = data.items || [];
    let html = pageHead('提交记录', '共 ' + (data.total || 0) + ' 条 · 点击查看详情并可重新评测');
    if (!items.length) {
      html += '<div class="panel">' + emptyState('暂无提交记录') + '</div>';
    } else {
      html += '<div class="panel"><div class="table-wrap"><table class="data"><thead><tr>' +
        '<th>ID</th><th>题目</th><th>状态</th><th>得分</th><th>用时</th><th>内存</th>' +
        '<th>选手</th><th>提交时间</th><th>评测次数</th></tr></thead><tbody>' +
        items.map(function (s) {
          return '<tr class="clickable" data-sid="' + esc(s.id) + '">' +
            '<td class="id-cell">' + esc(s.id) + '</td>' +
            '<td>' + esc(s.problem_title || ('#' + s.problem_id)) + '</td>' +
            '<td>' + statusBadge(s.status) + '</td>' +
            '<td class="mono">' + esc(s.score) + '</td>' +
            '<td class="mono">' + esc(fmtMs(s.time_used)) + '</td>' +
            '<td class="mono">' + esc(fmtMem(s.memory_used)) + '</td>' +
            '<td>' + esc(s.username || '-') + '</td>' +
            '<td class="time-cell">' + esc(s.submitted_at || '-') + '</td>' +
            '<td class="mono">' + esc(s.judge_times || 1) + '</td></tr>';
        }).join('') + '</tbody></table></div></div>';
    }
    $('#view').innerHTML = html;
    bindSubmissionRows();
  }

  async function openSubmissionDetail(id) {
    try {
      const s = await api('/api/submissions/' + id);
      openModal({
        title: '提交 #' + id + ' · ' + (s.problem_title || ('题目 #' + s.problem_id)),
        wide: true,
        body: '<div class="detail-list" style="margin-bottom:16px">' +
          '<dt>状态</dt><dd>' + statusBadge(s.status) + '</dd>' +
          '<dt>得分</dt><dd>' + esc(s.score || 0) + '</dd>' +
          '<dt>选手</dt><dd>' + esc(s.username || '-') + '</dd>' +
          '<dt>提交时间</dt><dd>' + esc(s.submitted_at || '-') + '</dd>' +
          '<dt>评测次数</dt><dd>' + esc(s.judge_times || 1) + '</dd>' +
          '<dt>源文件</dt><dd>' + esc(s.file_path || '-') + '</dd>' +
          '</div>' +
          judgeDetailHtml(s.judge_detail, s.status, s.score, s.time_used, s.memory_used) +
          '<div class="form-actions"><button class="btn btn-primary" id="rejudge-btn">重新评测</button></div>',
        onMount: function (overlay) {
          $('#rejudge-btn', overlay).addEventListener('click', async function () {
            const btn = this;
            btn.disabled = true;
            btn.textContent = '评测中…';
            try {
              const r = await api('/api/submissions/' + id + '/rejudge', { method: 'POST', body: {} });
              toast('重新评测完成', '状态 ' + r.status + ' · 得分 ' + r.score, 'ok');
              openSubmissionDetail(id);
            } catch (e) {
              toast('重新评测失败', e.message, 'err');
              btn.disabled = false;
              btn.textContent = '重新评测';
            }
          });
        }
      });
    } catch (e) {
      toast('加载失败', e.message, 'err');
    }
  }

  // ── 启动 ──────────────────────────────────────────────
  window.addEventListener('hashchange', render);
  document.addEventListener('DOMContentLoaded', function () {
    $('#refresh-btn').addEventListener('click', render);
    render();
    setInterval(refreshConn, 30000);
  });
})();
