/*!
 * markdown.js - 轻量 Markdown -> HTML 渲染器（无第三方依赖，离线可用）
 * 支持: 标题、段落、粗/斜/删除线、行内代码、围栏代码块、有序/无序列表（含嵌套）、
 *       表格、引用、分隔线、链接、图片，并默认转义所有原始 HTML。
 * 暴露: window.renderMarkdown(text)
 */
(function (global) {
  'use strict';

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function badUrl(url) {
    var u = String(url).trim();
    if (/^(javascript|vbscript|file):/i.test(u)) return true;
    if (/^data:/i.test(u) && !/^data:image\//i.test(u)) return true;
    return false;
  }

  // ── 行内解析（先替换代码/链接/图片，再转义其余文本） ──
  function inline(raw) {
    var slots = [];
    function slot(html) {
      slots.push(html);
      return '\u0001' + (slots.length - 1) + '\u0001';
    }

    var text = String(raw);

    // 行内代码
    text = text.replace(/`([^`\n]+)`/g, function (_, code) {
      return slot('<code>' + escapeHtml(code) + '</code>');
    });

    // 图片 ![alt](url)
    text = text.replace(/!\[([^\]\n]*)\]\(\s*([^)\s]+)\s*\)/g, function (_, alt, url) {
      if (badUrl(url)) return escapeHtml(alt);
      return slot('<img src="' + escapeHtml(url) + '" alt="' + escapeHtml(alt) +
                  '" loading="lazy" referrerpolicy="no-referrer">');
    });

    // 链接 [label](url)
    text = text.replace(/\[([^\]\n]+)\]\(\s*([^)\s]+)\s*\)/g, function (_, label, url) {
      if (badUrl(url)) return escapeHtml(label);
      return slot('<a href="' + escapeHtml(url) +
                  '" target="_blank" rel="noopener noreferrer">' + escapeHtml(label) + '</a>');
    });

    // 其余文本统一转义，杜绝原始 HTML 注入
    text = escapeHtml(text);

    // 强调
    text = text.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    text = text.replace(/__([^_]+)__/g, '<strong>$1</strong>');
    text = text.replace(/~~([^~]+)~~/g, '<del>$1</del>');
    text = text.replace(/(^|[^A-Za-z0-9*])\*([^*\n]+)\*/g, '$1<em>$2</em>');
    text = text.replace(/(^|[^A-Za-z0-9_])_([^_\n]+)_/g, '$1<em>$2</em>');

    // 还原占位
    text = text.replace(/\u0001(\d+)\u0001/g, function (_, i) { return slots[+i]; });
    return text;
  }

  function indentWidth(line) {
    var m = line.match(/^[ \t]*/)[0];
    return m.replace(/\t/g, '    ').length;
  }

  function splitCells(row) {
    var s = row.trim();
    if (s.charAt(0) === '|') s = s.slice(1);
    if (s.charAt(s.length - 1) === '|') s = s.slice(0, -1);
    return s.split('|').map(function (c) { return c.trim(); });
  }

  function isSeparatorRow(row) {
    if (row.indexOf('-') < 0) return false;
    var cells = splitCells(row);
    if (!cells.length) return false;
    return cells.every(function (c) { return c === '' || /^:?-{2,}:?$/.test(c); });
  }

  function isBlockStartAt(lines, i) {
    var l = lines[i];
    if (/^\s{0,3}(#{1,6})\s+/.test(l)) return true;
    if (/^\s*(`{3,}|~{3,})/.test(l)) return true;
    if (/^\s{0,3}>/.test(l)) return true;
    if (/^\s*([-*+]|\d+[.)])\s+/.test(l)) return true;
    if (/^\s{0,3}([-*_])(?:\s*\1){2,}\s*$/.test(l)) return true;
    if (l.indexOf('|') >= 0 && i + 1 < lines.length && isSeparatorRow(lines[i + 1])) return true;
    return false;
  }

  function parseTable(lines, start) {
    var header = splitCells(lines[start]);
    var aligns = splitCells(lines[start + 1]).map(function (c) {
      var left = c.charAt(0) === ':';
      var right = c.charAt(c.length - 1) === ':';
      if (left && right) return 'center';
      if (right) return 'right';
      if (left) return 'left';
      return '';
    });

    var body = [];
    var i = start + 2;
    while (i < lines.length && lines[i].indexOf('|') >= 0 && !/^\s*$/.test(lines[i])) {
      body.push(splitCells(lines[i]));
      i++;
    }

    var html = '<table><thead><tr>';
    for (var c = 0; c < header.length; c++) {
      html += '<th' + (aligns[c] ? ' style="text-align:' + aligns[c] + '"' : '') + '>' +
              inline(header[c]) + '</th>';
    }
    html += '</tr></thead><tbody>';
    for (var r = 0; r < body.length; r++) {
      html += '<tr>';
      for (var k = 0; k < header.length; k++) {
        html += '<td' + (aligns[k] ? ' style="text-align:' + aligns[k] + '"' : '') + '>' +
                inline(body[r][k] || '') + '</td>';
      }
      html += '</tr>';
    }
    html += '</tbody></table>';
    return { html: html, next: i };
  }

  function parseList(lines, start) {
    var itemRe = /^(\s*)([-*+]|\d+[.)])\s+(.*)$/;
    var first = lines[start].match(itemRe);
    var baseIndent = first[1].length;
    var items = [];
    var i = start;

    while (i < lines.length) {
      var line = lines[i];

      if (/^\s*$/.test(line)) {
        var j = i + 1;
        while (j < lines.length && /^\s*$/.test(lines[j])) j++;
        if (j < lines.length && itemRe.test(lines[j]) && indentWidth(lines[j]) >= baseIndent) {
          i = j;
          continue;
        }
        break;
      }

      var m = line.match(itemRe);
      if (!m) {
        if (items.length && indentWidth(line) > baseIndent) {
          items[items.length - 1].text += '\n' + line.trim();
          i++;
          continue;
        }
        break;
      }

      var ind = m[1].length;
      if (ind < baseIndent) break;

      if (ind > baseIndent) {
        var sub = parseList(lines, i);
        if (items.length) items[items.length - 1].children += sub.html;
        i = sub.next;
        continue;
      }

      var type = /\d/.test(m[2]) ? 'ol' : 'ul';
      var last = items[items.length - 1];
      if (last && last.type !== type) break;
      items.push({ type: type, text: m[3], children: '' });
      i++;
    }

    if (!items.length) return { html: '', next: i };
    var tag = items[0].type;
    var html = '<' + tag + '>';
    items.forEach(function (it) {
      var content = inline(it.text).replace(/\n/g, '<br>');
      if (it.children) content += it.children;
      html += '<li>' + content + '</li>';
    });
    html += '</' + tag + '>';
    return { html: html, next: i };
  }

  function render(src) {
    if (src === null || src === undefined) return '';
    var lines = String(src).replace(/\r\n?/g, '\n').replace(/\t/g, '    ').split('\n');
    var out = [];
    var i = 0;

    while (i < lines.length) {
      var line = lines[i];

      if (/^\s*$/.test(line)) { i++; continue; }

      // 围栏代码块
      var fence = line.match(/^\s*(`{3,}|~{3,})\s*([\w#.+-]*)\s*$/);
      if (fence) {
        var marker = fence[1].charAt(0);
        var count = fence[1].length;
        var lang = fence[2];
        var closeRe = new RegExp('^\\s*\\' + marker + '{' + count + ',}\\s*$');
        var buf = [];
        i++;
        while (i < lines.length && !closeRe.test(lines[i])) {
          buf.push(lines[i]);
          i++;
        }
        if (i < lines.length) i++;
        var cls = lang ? ' class="language-' + escapeHtml(lang) + '"' : '';
        out.push('<pre><code' + cls + '>' + escapeHtml(buf.join('\n')) + '</code></pre>');
        continue;
      }

      // 标题
      var heading = line.match(/^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$/);
      if (heading) {
        var level = heading[1].length;
        out.push('<h' + level + '>' + inline(heading[2]) + '</h' + level + '>');
        i++;
        continue;
      }

      // 分隔线
      if (/^\s{0,3}([-*_])(?:\s*\1){2,}\s*$/.test(line)) {
        out.push('<hr>');
        i++;
        continue;
      }

      // 表格
      if (line.indexOf('|') >= 0 && i + 1 < lines.length && isSeparatorRow(lines[i + 1])) {
        var table = parseTable(lines, i);
        out.push(table.html);
        i = table.next;
        continue;
      }

      // 引用
      if (/^\s{0,3}>/.test(line)) {
        var quoted = [];
        while (i < lines.length && (/^\s{0,3}>/.test(lines[i]) || /^\s*$/.test(lines[i]))) {
          if (/^\s*$/.test(lines[i]) && (i + 1 >= lines.length || !/^\s{0,3}>/.test(lines[i + 1]))) break;
          quoted.push(lines[i].replace(/^\s{0,3}> ?/, ''));
          i++;
        }
        out.push('<blockquote>' + render(quoted.join('\n')) + '</blockquote>');
        continue;
      }

      // 列表
      var listMatch = line.match(/^\s*([-*+]|\d+[.)])\s+/);
      if (listMatch) {
        var list = parseList(lines, i);
        out.push(list.html);
        i = list.next;
        continue;
      }

      // 段落
      var para = [];
      while (i < lines.length && !/^\s*$/.test(lines[i]) &&
             (para.length === 0 || !isBlockStartAt(lines, i))) {
        para.push(lines[i].trim());
        i++;
      }
      out.push('<p>' + para.map(inline).join('<br>') + '</p>');
    }

    return out.join('\n');
  }

  global.renderMarkdown = render;
})(window);
