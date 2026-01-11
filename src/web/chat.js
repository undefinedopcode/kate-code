// Chat state
let messages = {};
let bridge = null;

// Map file extensions to highlight.js language identifiers
const extToLanguage = {
    // Web
    'js': 'javascript', 'mjs': 'javascript', 'cjs': 'javascript',
    'ts': 'typescript', 'tsx': 'typescript', 'jsx': 'javascript',
    'html': 'xml', 'htm': 'xml', 'xhtml': 'xml',
    'css': 'css', 'scss': 'scss', 'sass': 'scss', 'less': 'less',
    'json': 'json', 'json5': 'json',
    // Systems
    'c': 'c', 'h': 'c',
    'cpp': 'cpp', 'cxx': 'cpp', 'cc': 'cpp', 'hpp': 'cpp', 'hxx': 'cpp',
    'rs': 'rust',
    'go': 'go',
    'zig': 'zig',
    // JVM
    'java': 'java', 'kt': 'kotlin', 'kts': 'kotlin', 'scala': 'scala',
    // Scripting
    'py': 'python', 'pyw': 'python', 'pyi': 'python',
    'rb': 'ruby', 'rake': 'ruby',
    'php': 'php',
    'pl': 'perl', 'pm': 'perl',
    'lua': 'lua',
    'sh': 'bash', 'bash': 'bash', 'zsh': 'bash', 'fish': 'fish',
    'ps1': 'powershell', 'psm1': 'powershell',
    // Config/Data
    'yaml': 'yaml', 'yml': 'yaml',
    'toml': 'ini', 'ini': 'ini', 'conf': 'ini',
    'xml': 'xml', 'svg': 'xml', 'xsd': 'xml', 'xsl': 'xml',
    'md': 'markdown', 'markdown': 'markdown',
    'sql': 'sql',
    // Build/DevOps
    'cmake': 'cmake', 'makefile': 'makefile', 'mk': 'makefile',
    'dockerfile': 'dockerfile',
    'gradle': 'gradle', 'groovy': 'groovy',
    // Other
    'swift': 'swift',
    'cs': 'csharp',
    'fs': 'fsharp', 'fsx': 'fsharp',
    'ex': 'elixir', 'exs': 'elixir',
    'erl': 'erlang', 'hrl': 'erlang',
    'hs': 'haskell',
    'ml': 'ocaml', 'mli': 'ocaml',
    'clj': 'clojure', 'cljs': 'clojure', 'cljc': 'clojure',
    'lisp': 'lisp', 'cl': 'lisp', 'el': 'lisp',
    'r': 'r',
    'dart': 'dart',
    'v': 'verilog', 'sv': 'verilog',
    'vhd': 'vhdl', 'vhdl': 'vhdl',
    'tex': 'latex', 'latex': 'latex',
    'diff': 'diff', 'patch': 'diff',
    'qml': 'qml',
    // Qt/KDE
    'pro': 'qmake', 'pri': 'qmake',
    'ui': 'xml', 'rc': 'xml', 'qrc': 'xml'
};

// Get highlight.js language from file path
function getLanguageFromPath(filePath) {
    if (!filePath) return null;
    const fileName = filePath.split('/').pop().toLowerCase();

    // Handle special filenames
    if (fileName === 'makefile' || fileName === 'gnumakefile') return 'makefile';
    if (fileName === 'dockerfile') return 'dockerfile';
    if (fileName === 'cmakelists.txt') return 'cmake';

    const ext = fileName.split('.').pop();
    return extToLanguage[ext] || null;
}

// Highlight code using highlight.js
function highlightCode(code, language) {
    if (typeof hljs === 'undefined') {
        return escapeHtml(code);
    }

    try {
        if (language && hljs.getLanguage(language)) {
            return hljs.highlight(code, { language: language }).value;
        } else {
            // Auto-detect if no language specified
            return hljs.highlightAuto(code).value;
        }
    } catch (e) {
        logToQt('Highlight error: ' + e);
        return escapeHtml(code);
    }
}

// Helper to log to C++ via bridge
function logToQt(message) {
    if (window.bridge && window.bridge.logFromJS) {
        window.bridge.logFromJS(message);
    }
    console.log(message);
}

// Configure marked.js with syntax highlighting
function configureMarked() {
    logToQt('Configuring marked.js...');
    logToQt('marked available: ' + (typeof marked !== 'undefined'));
    logToQt('hljs available: ' + (typeof hljs !== 'undefined'));

    if (typeof marked === 'undefined') {
        logToQt('ERROR: marked.js not loaded!');
        return;
    }

    if (typeof hljs === 'undefined') {
        logToQt('ERROR: highlight.js not loaded!');
        return;
    }

    // marked.js v11+ requires using a custom renderer with hooks
    const renderer = {
        code(code, infostring) {
            const lang = (infostring || '').match(/\S*/)[0];
            logToQt('Renderer code() called - lang: ' + lang + ', code length: ' + code.length);

            if (typeof hljs !== 'undefined') {
                let highlighted;
                if (lang && hljs.getLanguage(lang)) {
                    try {
                        highlighted = hljs.highlight(code, { language: lang }).value;
                        logToQt('Highlighted with language: ' + lang);
                    } catch (e) {
                        logToQt('Highlight error: ' + e);
                        highlighted = code;
                    }
                } else {
                    // Auto-detect language if not specified
                    try {
                        const result = hljs.highlightAuto(code);
                        highlighted = result.value;
                        logToQt('Auto-detected language: ' + result.language);
                    } catch (e) {
                        logToQt('Highlight auto error: ' + e);
                        highlighted = code;
                    }
                }

                // Base64 encode the code to safely store in data attribute
                const encodedCode = btoa(unescape(encodeURIComponent(code)));

                return '<div class="code-block-wrapper">' +
                       '<button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="' + encodedCode + '" title="Copy code">📋</button>' +
                       '<pre><code class="hljs language-' + lang + '">' + highlighted + '</code></pre>' +
                       '</div>';
            }

            const encodedCode = btoa(unescape(encodeURIComponent(code)));
            return '<div class="code-block-wrapper">' +
                   '<button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="' + encodedCode + '" title="Copy code">📋</button>' +
                   '<pre><code>' + code + '</code></pre>' +
                   '</div>';
        }
    };

    marked.use({
        breaks: true,
        gfm: true,
        renderer: renderer
    });

    logToQt('marked.js configured with custom renderer successfully');
}

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    console.log('Chat UI initialized');

    // Setup Qt WebChannel if available
    if (typeof QWebChannel !== 'undefined' && typeof qt !== 'undefined') {
        new QWebChannel(qt.webChannelTransport, (channel) => {
            window.bridge = channel.objects.bridge;
            logToQt('Qt WebChannel bridge connected');

            // Now that bridge is ready, configure marked
            configureMarked();

            // If libraries aren't loaded yet, try again after a delay
            if (typeof marked === 'undefined' || typeof hljs === 'undefined') {
                logToQt('Libraries not ready, retrying in 500ms...');
                setTimeout(configureMarked, 500);
            }
        });
    } else {
        console.warn('QWebChannel or qt not available');
        // Still try to configure marked even without bridge
        configureMarked();
    }
});

// Add a new message
function addMessage(id, role, content, timestamp, isStreaming) {
    const message = {
        id: id,
        role: role,
        content: content || '',
        timestamp: timestamp || new Date().toISOString(),
        isStreaming: isStreaming || false,
        toolCalls: []
    };

    messages[id] = message;
    renderMessage(message);
    scrollToBottom();
}

// Update message content (for streaming)
function updateMessage(id, content) {
    if (!messages[id]) return;

    messages[id].content += content;
    updateMessageDOM(id);
    scrollToBottom();
}

// Finish streaming
function finishMessage(id) {
    if (!messages[id]) return;

    messages[id].isStreaming = false;
    updateMessageDOM(id);
}

// Add tool call to message
function addToolCall(messageId, toolCallId, name, status, filePath, inputJson, oldText, newText, editsJson) {
    if (!messages[messageId]) return;

    // Parse input to extract command for Bash tools
    let input = {};
    try {
        input = inputJson ? JSON.parse(inputJson) : {};
    } catch (e) {
        console.warn('Failed to parse tool input JSON:', e);
    }

    // Parse edits array
    let edits = [];
    try {
        edits = editsJson ? JSON.parse(editsJson) : [];
    } catch (e) {
        console.warn('Failed to parse edits JSON:', e);
    }

    // Check if tool call already exists (avoid duplicates)
    const existing = messages[messageId].toolCalls.find(tc => tc.id === toolCallId);
    if (existing) {
        // Update existing tool call
        existing.status = status;
        existing.name = name || existing.name;
        existing.filePath = filePath || existing.filePath;
        existing.input = input;
        existing.oldText = oldText || '';
        existing.newText = newText || '';
        existing.edits = edits.length > 0 ? edits : existing.edits;
    } else {
        // Add new tool call at current content length position
        const toolCall = {
            id: toolCallId,
            name: name,
            status: status,
            filePath: filePath || '',
            input: input,
            result: '',
            position: messages[messageId].content.length,
            oldText: oldText || '',
            newText: newText || '',
            edits: edits
        };
        messages[messageId].toolCalls.push(toolCall);
    }

    updateMessageDOM(messageId);
    scrollToBottom();
}

// Update tool call status
function updateToolCall(messageId, toolCallId, status, result) {
    if (!messages[messageId]) return;

    let toolCall = messages[messageId].toolCalls.find(tc => tc.id === toolCallId);

    if (!toolCall) {
        // Tool call doesn't exist yet (happens with Write tool that only sends update)
        // Create it now with the result
        toolCall = {
            id: toolCallId,
            name: 'Write',  // Will be updated if we get more info
            status: status || 'completed',
            filePath: '',
            input: {},
            result: result || '',
            position: messages[messageId].content.length,
            oldText: '',
            newText: ''
        };
        messages[messageId].toolCalls.push(toolCall);
    } else {
        // Update existing tool call
        if (status) {
            toolCall.status = status;
        }
        // Only update result if we have a non-empty one (don't overwrite with empty)
        if (result) {
            toolCall.result = result;
        }
    }

    updateMessageDOM(messageId);
}

// Render a message to DOM
function renderMessage(message) {
    const container = document.getElementById('messages');

    const messageEl = document.createElement('div');
    messageEl.className = `message ${message.role}`;
    if (message.isStreaming) {
        messageEl.classList.add('streaming');
    }
    messageEl.id = `message-${message.id}`;

    messageEl.innerHTML = createMessageHTML(message);
    container.appendChild(messageEl);
}

// Update existing message in DOM
function updateMessageDOM(id) {
    const message = messages[id];
    if (!message) return;

    const messageEl = document.getElementById(`message-${id}`);
    if (!messageEl) return;

    messageEl.className = `message ${message.role}`;
    if (message.isStreaming) {
        messageEl.classList.add('streaming');
    }

    messageEl.innerHTML = createMessageHTML(message);
}

// Create HTML for a message with inline tool calls
function createMessageHTML(message) {
    const timestamp = new Date(message.timestamp).toLocaleTimeString();

    let html = `<div class="message-header">
    <span class="message-role ${message.role}">${message.role}</span>
    <span class="message-timestamp">${timestamp}</span>
</div>
<div class="message-content">`;

    // For assistant messages, interleave content and tool calls
    if (message.role === 'assistant' && message.toolCalls && message.toolCalls.length > 0) {
        // Sort tool calls by position
        const sortedCalls = [...message.toolCalls].sort((a, b) => a.position - b.position);

        let lastPos = 0;
        for (const toolCall of sortedCalls) {
            // Add content before this tool call
            const textBefore = message.content.substring(lastPos, toolCall.position);
            if (textBefore) {
                if (typeof marked !== 'undefined') {
                    html += marked.parse(textBefore);
                } else {
                    html += escapeHtml(textBefore);
                }
            }

            // Add tool call inline
            html += renderToolCall(toolCall);
            lastPos = toolCall.position;
        }

        // Add remaining content after last tool call
        const textAfter = message.content.substring(lastPos);
        if (textAfter) {
            if (typeof marked !== 'undefined') {
                html += marked.parse(textAfter);
            } else {
                html += escapeHtml(textAfter);
            }
        }
    } else {
        // No tool calls or not assistant - render content normally
        if (message.role === 'assistant' && typeof marked !== 'undefined' && message.content) {
            html += marked.parse(message.content);
        } else {
            // For user/system messages, trim to avoid leading/trailing whitespace
            const content = message.content || '';
            html += escapeHtml(message.role === 'user' || message.role === 'system' ? content.trim() : content);
        }
    }

    html += '</div>';
    return html;
}

// Render a single tool call as inline element
function renderToolCall(toolCall) {
    const fileName = toolCall.filePath ? toolCall.filePath.split('/').pop() : '';
    const isExpanded = toolCall.expanded || false;

    // Extract command for Bash tools
    let commandDisplay = '';
    if (toolCall.name === 'Bash' && toolCall.input && toolCall.input.command) {
        const cmd = toolCall.input.command;
        // Show first 50 chars of command
        commandDisplay = cmd.length > 50 ? cmd.substring(0, 50) + '...' : cmd;
    }

    let html = `
        <div class="tool-call-inline ${toolCall.status}" data-tool-id="${escapeHtml(toolCall.id)}">
            <div class="tool-call-summary" onclick="toggleToolCall('${escapeHtml(toolCall.id)}')">
                <span class="tool-call-icon">🔧</span>
                <span class="tool-call-name">${escapeHtml(toolCall.name || 'Tool')}</span>
                ${commandDisplay ? `<span class="tool-call-command">${escapeHtml(commandDisplay)}</span>` : ''}
                ${fileName ? `<span class="tool-call-file">${escapeHtml(fileName)}</span>` : ''}
                <span class="tool-call-toggle">${isExpanded ? '▼' : '▶'}</span>
            </div>
    `;

    if (isExpanded) {
        html += '<div class="tool-call-details">';

        // Show full command for Bash tools
        if (toolCall.name === 'Bash' && toolCall.input && toolCall.input.command) {
            html += `<div class="tool-call-input"><strong>Command:</strong><pre>${escapeHtml(toolCall.input.command)}</pre></div>`;
        }

        // Show Edit tool as unified diff(s)
        if (toolCall.name === 'Edit') {
            // Check if we have multiple edits array
            if (toolCall.edits && toolCall.edits.length > 0) {
                html += `<div class="tool-call-input">
                    <strong>Diff${toolCall.edits.length > 1 ? 's' : ''}:</strong>`;

                for (let i = 0; i < toolCall.edits.length; i++) {
                    const edit = toolCall.edits[i];
                    const editFileName = edit.filePath || fileName || `edit ${i + 1}`;
                    const diff = generateUnifiedDiff(edit.oldText, edit.newText, editFileName);

                    if (toolCall.edits.length > 1) {
                        html += `<div class="edit-section">
                            <div class="edit-header">Edit ${i + 1} of ${toolCall.edits.length}${edit.filePath ? ': ' + escapeHtml(edit.filePath) : ''}</div>
                            <pre class="diff">${diff}</pre>
                        </div>`;
                    } else {
                        html += `<pre class="diff">${diff}</pre>`;
                    }
                }

                html += `</div>`;
            } else if (toolCall.oldText !== undefined && toolCall.newText !== undefined) {
                // Backward compatibility: single edit with oldText/newText
                const diff = generateUnifiedDiff(toolCall.oldText, toolCall.newText, fileName);
                html += `<div class="tool-call-input">
                    <strong>Diff:</strong>
                    <pre class="diff">${diff}</pre>
                </div>`;
            }
        }

        // Show Write tool content with syntax highlighting
        if (toolCall.name === 'Write' && toolCall.newText) {
            const language = getLanguageFromPath(toolCall.filePath);
            const highlighted = highlightCode(toolCall.newText, language);
            const encodedCode = btoa(unescape(encodeURIComponent(toolCall.newText)));
            html += `<div class="tool-call-input">
                <strong>Content:</strong>
                <div class="code-block-wrapper">
                    <button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="${encodedCode}" title="Copy code">📋</button>
                    <pre><code class="hljs${language ? ' language-' + language : ''}">${highlighted}</code></pre>
                </div>
            </div>`;
        }

        // Show result if available (skip for Write since we show content above)
        if (toolCall.result && toolCall.name !== 'Write') {
            html += `<div class="tool-call-result-section"><strong>Result:</strong><pre class="tool-call-result">${escapeHtml(toolCall.result)}</pre></div>`;
        }

        html += '</div>';
    }

    html += '</div>';
    return html;
}

// Generate unified diff using Myers' diff algorithm (similar to git diff)
function generateUnifiedDiff(oldText, newText, fileName) {
    const oldLines = oldText.split('\n');
    const newLines = newText.split('\n');

    // Compute LCS-based diff using Myers' algorithm
    const diff = computeDiff(oldLines, newLines);

    let result = [];
    result.push(`<span class="diff-header">--- ${escapeHtml(fileName || 'file')}</span>`);
    result.push(`<span class="diff-header">+++ ${escapeHtml(fileName || 'file')}</span>`);

    // Render diff with context
    const contextLines = 3;
    let i = 0;

    while (i < diff.length) {
        // Find next change
        while (i < diff.length && diff[i].type === 'equal') {
            i++;
        }

        if (i >= diff.length) break;

        // Start of hunk - show context before
        const hunkStart = Math.max(0, i - contextLines);

        // Find end of continuous changes
        let j = i;
        while (j < diff.length && (diff[j].type !== 'equal' ||
               (j + 1 < diff.length && diff[j + 1].type !== 'equal' && j < i + 20))) {
            j++;
        }

        // Show context after
        const hunkEnd = Math.min(diff.length, j + contextLines);

        // Render hunk
        for (let k = hunkStart; k < hunkEnd; k++) {
            const item = diff[k];
            if (item.type === 'equal') {
                result.push(`<span class="diff-context"> ${escapeHtml(item.value)}</span>`);
            } else if (item.type === 'delete') {
                result.push(`<span class="diff-remove">-${escapeHtml(item.value)}</span>`);
            } else if (item.type === 'insert') {
                result.push(`<span class="diff-add">+${escapeHtml(item.value)}</span>`);
            }
        }

        i = hunkEnd;
    }

    return result.join('');
}

// Compute diff using LCS (Longest Common Subsequence) approach
function computeDiff(oldLines, newLines) {
    const n = oldLines.length;
    const m = newLines.length;

    // Build LCS table using dynamic programming
    const lcs = Array(n + 1).fill(null).map(() => Array(m + 1).fill(0));

    for (let i = 1; i <= n; i++) {
        for (let j = 1; j <= m; j++) {
            if (oldLines[i - 1] === newLines[j - 1]) {
                lcs[i][j] = lcs[i - 1][j - 1] + 1;
            } else {
                lcs[i][j] = Math.max(lcs[i - 1][j], lcs[i][j - 1]);
            }
        }
    }

    // Backtrack to build diff
    const diff = [];
    let i = n, j = m;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] === newLines[j - 1]) {
            diff.unshift({ type: 'equal', value: oldLines[i - 1] });
            i--;
            j--;
        } else if (j > 0 && (i === 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
            diff.unshift({ type: 'insert', value: newLines[j - 1] });
            j--;
        } else if (i > 0) {
            diff.unshift({ type: 'delete', value: oldLines[i - 1] });
            i--;
        }
    }

    return diff;
}

// Toggle tool call expansion
function toggleToolCall(toolCallId) {
    // Find the tool call in messages
    for (const messageId in messages) {
        const toolCall = messages[messageId].toolCalls.find(tc => tc.id === toolCallId);
        if (toolCall) {
            toolCall.expanded = !toolCall.expanded;
            updateMessageDOM(messageId);
            break;
        }
    }
}

// Clear all messages
function clearMessages() {
    messages = {};
    document.getElementById('messages').innerHTML = '';
}

// Update todos display
function updateTodos(todosJson) {
    let todos;
    try {
        todos = JSON.parse(todosJson);
    } catch (e) {
        console.error('Failed to parse todos JSON:', e);
        return;
    }

    const container = document.getElementById('todos-container');
    if (!container) return;

    if (!todos || todos.length === 0) {
        container.innerHTML = '';
        container.style.display = 'none';
        return;
    }

    container.style.display = 'block';

    // Check if collapsed state is stored
    const isCollapsed = localStorage.getItem('todos-collapsed') === 'true';

    // Count completed vs total
    const completedCount = todos.filter(t => t.status === 'completed').length;
    const totalCount = todos.length;

    let html = `
        <div class="todos-header" onclick="toggleTodos()">
            <span class="todos-title">Tasks (${completedCount}/${totalCount})</span>
            <span class="todos-toggle">${isCollapsed ? '▲' : '▼'}</span>
        </div>
        <div class="todos-content ${isCollapsed ? 'collapsed' : ''}">
            <div class="todos-list">
    `;

    for (const todo of todos) {
        const status = todo.status || 'pending';
        const content = todo.content || '';
        const activeForm = todo.activeForm || content;
        const displayText = status === 'in_progress' ? activeForm : content;

        let statusIcon = '';
        if (status === 'completed') {
            statusIcon = '✓';
        } else if (status === 'in_progress') {
            statusIcon = '⟳';
        } else {
            statusIcon = '○';
        }

        html += `
            <div class="todo-item ${status}">
                <span class="todo-icon">${statusIcon}</span>
                <span class="todo-text">${escapeHtml(displayText)}</span>
            </div>
        `;
    }

    html += '</div></div>';
    container.innerHTML = html;
}

// Toggle todos collapsed state
function toggleTodos() {
    const content = document.querySelector('.todos-content');
    const toggle = document.querySelector('.todos-toggle');

    if (!content || !toggle) return;

    const isCollapsed = content.classList.toggle('collapsed');
    toggle.textContent = isCollapsed ? '▲' : '▼';

    // Save state
    localStorage.setItem('todos-collapsed', isCollapsed.toString());
}

// Scroll to bottom
function scrollToBottom() {
    window.scrollTo(0, document.body.scrollHeight);
}

// Escape HTML
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Copy code to clipboard
function copyCode(button) {
    // Decode base64 encoded code
    const encodedCode = button.getAttribute('data-code-b64');
    const code = decodeURIComponent(escape(atob(encodedCode)));

    logToQt('Copying code, length: ' + code.length);

    // Create a temporary textarea to copy from
    const textarea = document.createElement('textarea');
    textarea.value = code;
    textarea.style.position = 'fixed';
    textarea.style.opacity = '0';
    document.body.appendChild(textarea);
    textarea.select();

    try {
        document.execCommand('copy');

        // Visual feedback
        const originalText = button.textContent;
        button.textContent = '✓';
        button.classList.add('copied');

        setTimeout(() => {
            button.textContent = originalText;
            button.classList.remove('copied');
        }, 2000);

        logToQt('Code copied to clipboard successfully');
    } catch (err) {
        logToQt('Failed to copy code: ' + err);
    } finally {
        document.body.removeChild(textarea);
    }
}

// Apply color scheme
function applyColorScheme(cssVars) {
    const root = document.documentElement;
    const vars = cssVars.split(';').filter(v => v.trim());

    for (const varDecl of vars) {
        const [name, value] = varDecl.split(':').map(s => s.trim());
        if (name && value) {
            root.style.setProperty(name, value);
        }
    }

    console.log('Color scheme applied');
}

// Apply highlight.js theme
function applyHighlightTheme(themePath) {
    const linkElement = document.getElementById('highlight-theme');
    if (linkElement) {
        linkElement.href = themePath;
        logToQt('Applied highlight theme: ' + themePath);
    } else {
        logToQt('ERROR: highlight-theme link element not found');
    }
}

// Apply custom highlight.js CSS from Kate theme
function applyCustomHighlightCSS(cssText) {
    // Disable all existing stylesheets that contain highlight.js themes
    const allStyleSheets = document.styleSheets;
    for (let i = 0; i < allStyleSheets.length; i++) {
        const sheet = allStyleSheets[i];
        if (sheet.href && (sheet.href.includes('atom-one') || sheet.href.includes('highlight'))) {
            sheet.disabled = true;
            logToQt('Disabled bundled highlight theme: ' + sheet.href);
        }
    }

    // Remove the link element (fallback theme)
    const linkElement = document.getElementById('highlight-theme');
    if (linkElement) {
        linkElement.remove();
    }

    // Create or update style element for custom CSS
    let styleElement = document.getElementById('kate-highlight-theme');
    if (!styleElement) {
        styleElement = document.createElement('style');
        styleElement.id = 'kate-highlight-theme';
        document.head.appendChild(styleElement);
    }

    styleElement.textContent = cssText;
    logToQt('Applied Kate theme CSS: ' + cssText.length + ' bytes');
    logToQt('CSS preview: ' + cssText.substring(0, 200));
}

// Show inline permission request
function showPermissionRequest(requestId, toolName, input, options) {
    console.log('showPermissionRequest called:', requestId, toolName);
    console.log('Options:', options);

    if (!Array.isArray(options)) {
        console.error('Options is not an array:', options);
        return;
    }

    // Special handling for plan field - render as markdown
    let detailsHtml = '';
    if (typeof input === 'object' && input.plan) {
        // Render plan as markdown
        if (typeof marked !== 'undefined') {
            detailsHtml = `<div class="permission-plan">${marked.parse(input.plan)}</div>`;
        } else {
            detailsHtml = `<pre>${escapeHtml(input.plan)}</pre>`;
        }
    } else {
        // Show JSON for other inputs
        const inputDisplay = typeof input === 'object'
            ? JSON.stringify(input, null, 2)
            : String(input);
        detailsHtml = `<pre>${escapeHtml(inputDisplay)}</pre>`;
    }

    let html = `
        <div class="permission-request" id="perm-${requestId}">
            <div class="permission-header">
                <span class="permission-icon">🔐</span>
                <span class="permission-title">Permission Required: ${escapeHtml(toolName)}</span>
            </div>
            <div class="permission-body">
                <div class="permission-details">
                    ${detailsHtml}
                </div>
                <div class="permission-options">
    `;

    options.forEach((option, index) => {
        const label = option.name || option.label || 'Option';
        const description = option.description || '';
        const optionId = option.optionId || option.id || index.toString();
        html += `
            <div class="permission-option" onclick="respondToPermission(${requestId}, '${escapeHtml(optionId)}')">
                <div class="permission-option-label">${escapeHtml(label)}</div>
                ${description ? `<div class="permission-option-desc">${escapeHtml(description)}</div>` : ''}
            </div>
        `;
    });

    html += `
                </div>
            </div>
        </div>
    `;

    const container = document.getElementById('messages');
    if (!container) {
        console.error('Messages container not found');
        return;
    }

    const permEl = document.createElement('div');
    permEl.innerHTML = html;
    container.appendChild(permEl.firstElementChild);
    console.log('Permission request added to DOM');
    scrollToBottom();
}

// Respond to permission request
function respondToPermission(requestId, optionId) {
    // Remove the permission UI
    const permEl = document.getElementById(`perm-${requestId}`);
    if (permEl) {
        permEl.remove();
    }

    // Call back to C++ via WebChannel
    if (window.bridge) {
        window.bridge.respondToPermission(requestId, optionId);
    } else {
        console.error('WebChannel bridge not available');
    }
}

// Make functions available globally for Qt calls
window.addMessage = addMessage;
window.updateMessage = updateMessage;
window.finishMessage = finishMessage;
window.addToolCall = addToolCall;
window.updateToolCall = updateToolCall;
window.clearMessages = clearMessages;
window.updateTodos = updateTodos;
window.applyColorScheme = applyColorScheme;
window.toggleToolCall = toggleToolCall;
window.showPermissionRequest = showPermissionRequest;
window.copyCode = copyCode;
