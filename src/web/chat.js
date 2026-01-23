// Chat state
let messages = {};
let terminals = {};  // Terminal output state keyed by terminalId
let bridge = null;

// Material Symbols icon helper - returns HTML span with icon ligature
function materialIcon(name, extraClass = '') {
    const cls = extraClass ? `material-icon ${extraClass}` : 'material-icon';
    return `<span class="${cls}">${name}</span>`;
}

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

// Check if a tool is a Bash/terminal tool that should get ANSI color processing
function isBashTool(toolName) {
    if (!toolName) return false;
    const name = toolName.toLowerCase();
    return toolName === 'Bash' ||
           toolName === 'mcp__acp__Bash' ||
           name.includes('bash') ||
           name.includes('shell') ||
           name.includes('terminal');
}

// Parse Bash tool result to extract exit code and output
// Input format: "Exited with code X.Final output:\n\n<output>"
// Returns: { exitCode: number|null, output: string }
function parseBashResult(result) {
    if (!result) return { exitCode: null, output: '' };

    // Match "Exited with code X.Final output:" or "Exited with code X.Final output:"
    const match = result.match(/^Exited with code (\d+)\.Final output:\n?\n?([\s\S]*)$/);
    if (match) {
        return {
            exitCode: parseInt(match[1], 10),
            output: match[2] || ''
        };
    }

    // No match - return raw result
    return { exitCode: null, output: result };
}

// Check if a tool is a Read tool (standard or MCP variant)
function isReadTool(toolName) {
    return toolName === 'Read' || toolName === 'mcp__acp__Read';
}

// Check if a tool is a Write tool (standard or MCP variant)
function isWriteTool(toolName) {
    return toolName === 'Write' || toolName === 'mcp__acp__Write';
}

// Check if a tool is an Edit tool (standard or MCP variant)
function isEditTool(toolName) {
    return toolName === 'Edit' || toolName === 'mcp__acp__Edit';
}

// Check if a tool is a Kate MCP tool (mcp__acp__ prefix)
function isKateTool(toolName) {
    return toolName && toolName.startsWith('mcp__acp__');
}

// Get display name for a tool (strips mcp__acp__ prefix if present)
function getToolDisplayName(toolName) {
    if (!toolName) return 'Tool';
    if (toolName.startsWith('mcp__acp__')) {
        return toolName.substring('mcp__acp__'.length);
    }
    return toolName;
}

// Clean Read tool result by removing system-reminder tags and line number prefixes
function cleanReadResult(text) {
    // Remove <system-reminder>...</system-reminder> blocks (including multiline)
    let cleaned = text.replace(/<system-reminder>[\s\S]*?<\/system-reminder>/g, '');

    // Strip leading triple backticks with optional language identifier
    cleaned = cleaned.replace(/^```[a-zA-Z0-9]*\n?/, '');

    // Strip trailing triple backticks
    cleaned = cleaned.replace(/\n?```\s*$/, '');

    // Strip line number prefixes (e.g., "   921→" or "  42→")
    // Pattern: optional spaces, digits, arrow (→), then the actual content
    const lines = cleaned.split('\n');
    const strippedLines = lines.map(line => {
        const match = line.match(/^\s*\d+→(.*)$/);
        return match ? match[1] : line;
    });

    // Remove trailing empty lines that result from stripping
    while (strippedLines.length > 0 && strippedLines[strippedLines.length - 1].trim() === '') {
        strippedLines.pop();
    }

    return strippedLines.join('\n');
}

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

// Split highlighted HTML into lines while preserving HTML tag structure
// Handles tags that span multiple lines by closing and reopening them at line boundaries
function splitHighlightedLines(html, expectedCount) {
    const lines = [];
    let currentLine = '';
    let openTags = [];  // Stack of open tag names

    let i = 0;
    while (i < html.length) {
        const char = html[i];

        if (char === '\n') {
            // Close all open tags for this line
            let closingTags = '';
            for (let t = openTags.length - 1; t >= 0; t--) {
                closingTags += '</span>';
            }
            lines.push(currentLine + closingTags);

            // Start new line with reopened tags
            currentLine = '';
            for (let t = 0; t < openTags.length; t++) {
                // Re-open with the same class - we need to track full opening tag
                currentLine += `<span class="${openTags[t]}">`;
            }
            i++;
        } else if (char === '<') {
            // Parse HTML tag
            const tagEnd = html.indexOf('>', i);
            if (tagEnd === -1) {
                currentLine += char;
                i++;
                continue;
            }

            const tagContent = html.substring(i + 1, tagEnd);
            const fullTag = html.substring(i, tagEnd + 1);

            if (tagContent.startsWith('/')) {
                // Closing tag
                openTags.pop();
                currentLine += fullTag;
            } else if (tagContent.startsWith('span')) {
                // Opening span tag - extract class
                const classMatch = tagContent.match(/class="([^"]+)"/);
                const className = classMatch ? classMatch[1] : '';
                openTags.push(className);
                currentLine += fullTag;
            } else {
                // Other tag (shouldn't happen with hljs output)
                currentLine += fullTag;
            }
            i = tagEnd + 1;
        } else {
            currentLine += char;
            i++;
        }
    }

    // Don't forget the last line (if no trailing newline)
    if (currentLine || lines.length < expectedCount) {
        // Close any remaining open tags
        let closingTags = '';
        for (let t = openTags.length - 1; t >= 0; t--) {
            closingTags += `</span>`;
        }
        lines.push(currentLine + closingTags);
    }

    // Ensure we have the expected number of lines
    while (lines.length < expectedCount) {
        lines.push('');
    }

    return lines;
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
                       '<button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="' + encodedCode + '" title="Copy code"><span class="material-icon material-icon-sm">content_copy</span></button>' +
                       '<pre><code class="hljs language-' + lang + '">' + highlighted + '</code></pre>' +
                       '</div>';
            }

            const encodedCode = btoa(unescape(encodeURIComponent(code)));
            return '<div class="code-block-wrapper">' +
                   '<button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="' + encodedCode + '" title="Copy code"><span class="material-icon material-icon-sm">content_copy</span></button>' +
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
            bridge = channel.objects.bridge;
            window.bridge = bridge;
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
function addMessage(id, role, content, timestamp, isStreaming, images) {
    const message = {
        id: id,
        role: role,
        content: content || '',
        timestamp: timestamp || new Date().toISOString(),
        isStreaming: isStreaming || false,
        toolCalls: [],
        images: images || []  // Array of {data, mimeType, width, height}
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
function addToolCall(messageId, toolCallId, name, status, filePath, inputJson, oldText, newText, editsJson, terminalId) {
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
        existing.terminalId = terminalId || existing.terminalId;
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
            edits: edits,
            terminalId: terminalId || ''
        };
        messages[messageId].toolCalls.push(toolCall);
    }

    updateMessageDOM(messageId);
    scrollToBottom();
}

// Update tool call status - result is Base64 encoded to handle ANSI escape codes
function updateToolCall(messageId, toolCallId, status, base64Result) {
    if (!messages[messageId]) return;

    // Decode base64 result
    let result = '';
    if (base64Result) {
        try {
            result = decodeURIComponent(escape(atob(base64Result)));
        } catch (e) {
            console.error('Failed to decode tool result:', e);
            result = base64Result;  // Fall back to raw value
        }
    }

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
        const content = message.content || '';
        if ((message.role === 'assistant' || message.role === 'user') && typeof marked !== 'undefined' && content) {
            const rendered = marked.parse(content.trim()).trim();
            if (message.role === 'user') {
                html += `<div class="user-markdown">${rendered}</div>`;
            } else {
                html += rendered;
            }
        } else {
            // For system messages, just escape HTML
            html += escapeHtml(message.role === 'system' ? content.trim() : content);
        }

        // Render images for user messages (below text content)
        if (message.role === 'user' && message.images && message.images.length > 0) {
            html += '<div class="message-images">';
            for (const img of message.images) {
                const dataUrl = `data:${img.mimeType};base64,${img.data}`;
                html += `<img src="${dataUrl}" class="message-image" alt="Attached image" style="max-width: 300px; max-height: 300px; border-radius: 4px; margin-top: 8px;">`;
            }
            html += '</div>';
        }
    }

    html += '</div>';
    return html;
}

// Render a single tool call as inline element
function renderToolCall(toolCall) {
    const fileName = toolCall.filePath ? toolCall.filePath.split('/').pop() : '';
    // Edit tools are expanded by default to show the diff inline
    const isExpanded = toolCall.expanded !== undefined ? toolCall.expanded : isEditTool(toolCall.name);

    // Extract command for Bash tools (including MCP variants like mcp__acp__Bash)
    let commandDisplay = '';
    if (isBashTool(toolCall.name) && toolCall.input && toolCall.input.command) {
        const cmd = toolCall.input.command;
        // Show first 50 chars of command
        commandDisplay = cmd.length > 50 ? cmd.substring(0, 50) + '...' : cmd;
    }

    // Select icon based on tool type (Material Symbols names)
    let toolIconName = 'build'; // default (wrench/tool icon)
    if (toolCall.name === 'Task') toolIconName = 'smart_toy';
    else if (toolCall.name === 'TaskOutput') toolIconName = 'download';
    else if (isBashTool(toolCall.name)) toolIconName = 'terminal';
    else if (isEditTool(toolCall.name)) toolIconName = 'edit';
    else if (isWriteTool(toolCall.name)) toolIconName = 'edit_document';
    else if (isReadTool(toolCall.name)) toolIconName = 'description';
    else if (toolCall.name === 'Glob' || toolCall.name === 'Grep') toolIconName = 'search';
    const toolIcon = materialIcon(toolIconName, 'material-icon-sm');

    // Determine extra CSS classes for Task/TaskOutput
    let extraClasses = '';
    if (toolCall.name === 'Task') extraClasses = ' task-tool';
    else if (toolCall.name === 'TaskOutput') extraClasses = ' task-output-tool';

    // Build Task-specific summary elements
    let taskSummaryHtml = '';
    if (toolCall.name === 'Task' && toolCall.input) {
        const subagentType = toolCall.input.subagent_type || 'general-purpose';
        const description = toolCall.input.description || '';
        const isBackground = toolCall.input.run_in_background === true;
        const isResuming = !!toolCall.input.resume;

        // Subagent type badge
        const badgeClass = 'task-badge-' + subagentType.toLowerCase().replace(/[^a-z]/g, '-');
        taskSummaryHtml += `<span class="task-badge ${badgeClass}">${escapeHtml(subagentType)}</span>`;

        if (isBackground) {
            taskSummaryHtml += `<span class="task-indicator task-background" title="Running in background">${materialIcon('bolt', 'material-icon-sm')}</span>`;
        }
        if (isResuming) {
            taskSummaryHtml += `<span class="task-indicator task-resume" title="Resuming previous agent">${materialIcon('refresh', 'material-icon-sm')}</span>`;
        }
        if (description) {
            taskSummaryHtml += `<span class="task-description">${escapeHtml(description)}</span>`;
        }
    }

    // Build TaskOutput-specific summary elements
    let taskOutputSummaryHtml = '';
    if (toolCall.name === 'TaskOutput' && toolCall.input) {
        const taskId = toolCall.input.task_id || '';
        const blocking = toolCall.input.block !== false; // Default is blocking
        const shortId = taskId.length > 12 ? taskId.substring(0, 8) + '...' : taskId;

        taskOutputSummaryHtml += `<span class="task-output-id" title="${escapeHtml(taskId)}">${escapeHtml(shortId)}</span>`;
        taskOutputSummaryHtml += `<span class="task-indicator ${blocking ? 'task-blocking' : 'task-nonblocking'}" title="${blocking ? 'Waiting for completion' : 'Non-blocking'}">${materialIcon(blocking ? 'hourglass_empty' : 'sync', 'material-icon-sm')}</span>`;
    }

    // Get display name (strip mcp__acp__ prefix) and check if Kate tool
    const displayName = getToolDisplayName(toolCall.name);
    const isKate = isKateTool(toolCall.name);

    let html = `
        <div class="tool-call-inline ${toolCall.status}${extraClasses}" data-tool-id="${escapeHtml(toolCall.id)}">
            <div class="tool-call-summary" onclick="toggleToolCall('${escapeHtml(toolCall.id)}')">
                <span class="tool-call-icon">${toolIcon}</span>
                <span class="tool-call-name">${escapeHtml(displayName)}</span>
                ${taskSummaryHtml}
                ${taskOutputSummaryHtml}
                ${commandDisplay ? `<span class="tool-call-command">${escapeHtml(commandDisplay)}</span>` : ''}
                ${fileName ? `<span class="tool-call-file">${escapeHtml(fileName)}</span>` : ''}
                ${isKate ? '<span class="tool-call-kate-badge">Kate</span>' : ''}
                <span class="tool-call-toggle">${materialIcon(isExpanded ? 'expand_more' : 'chevron_right', 'material-icon-sm')}</span>
            </div>
    `;

    if (isExpanded) {
        html += '<div class="tool-call-details">';

        // Show full command for Bash tools (including MCP variants)
        if (isBashTool(toolCall.name) && toolCall.input && toolCall.input.command) {
            html += `<div class="tool-call-input"><strong>Command:</strong><pre>${escapeHtml(toolCall.input.command)}</pre></div>`;
        }

        // Show Edit tool as unified diff(s)
        if (isEditTool(toolCall.name)) {
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
        if (isWriteTool(toolCall.name) && toolCall.newText) {
            const language = getLanguageFromPath(toolCall.filePath);
            const highlighted = highlightCode(toolCall.newText, language);
            const encodedCode = btoa(unescape(encodeURIComponent(toolCall.newText)));
            html += `<div class="tool-call-input">
                <strong>Content:</strong>
                <div class="code-block-wrapper">
                    <button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="${encodedCode}" title="Copy code"><span class="material-icon material-icon-sm">content_copy</span></button>
                    <pre><code class="hljs${language ? ' language-' + language : ''}">${highlighted}</code></pre>
                </div>
            </div>`;
        }

        // Show Task tool details
        if (toolCall.name === 'Task' && toolCall.input) {
            const prompt = toolCall.input.prompt || '';
            const model = toolCall.input.model;

            html += `<div class="tool-call-input task-details">`;

            if (model) {
                html += `<div class="task-model-info"><strong>Model:</strong> ${escapeHtml(model)}</div>`;
            }

            if (prompt) {
                html += `<strong>Prompt:</strong><pre class="task-prompt">${escapeHtml(prompt)}</pre>`;
            }

            html += `</div>`;
        }

        // Show TaskOutput tool details
        if (toolCall.name === 'TaskOutput' && toolCall.input) {
            const taskId = toolCall.input.task_id || '';
            const timeout = toolCall.input.timeout;

            html += `<div class="tool-call-input task-output-details">`;
            html += `<strong>Task ID:</strong> <code>${escapeHtml(taskId)}</code>`;
            if (timeout) {
                html += `<br><strong>Timeout:</strong> ${timeout}ms`;
            }
            html += `</div>`;
        }

        // Show result if available (skip for Write/Edit since we show content above)
        // Also skip if we have terminal output (terminal replaces the result display)
        if (toolCall.result && !isWriteTool(toolCall.name) && !isEditTool(toolCall.name) && !toolCall.terminalId) {
            if (isReadTool(toolCall.name)) {
                // For Read tool, clean and highlight the result
                const cleanedCode = cleanReadResult(toolCall.result);
                const language = getLanguageFromPath(toolCall.filePath);
                const highlighted = highlightCode(cleanedCode, language);
                const encodedCode = btoa(unescape(encodeURIComponent(cleanedCode)));
                html += `<div class="tool-call-result-section">
                    <strong>Result:</strong>
                    <div class="code-block-wrapper">
                        <button class="code-copy-btn" onclick="copyCode(this)" data-code-b64="${encodedCode}" title="Copy code"><span class="material-icon material-icon-sm">content_copy</span></button>
                        <pre><code class="hljs${language ? ' language-' + language : ''}">${highlighted}</code></pre>
                    </div>
                </div>`;
            } else if (isBashTool(toolCall.name)) {
                // Bash/terminal tools - parse exit code and render with structured layout
                const parsed = parseBashResult(toolCall.result);
                const exitCodeClass = parsed.exitCode === 0 ? 'exit-success' : (parsed.exitCode !== null ? 'exit-error' : '');

                html += `<div class="tool-call-result-section bash-result-section ${exitCodeClass}">`;

                // Show exit code if available
                if (parsed.exitCode !== null) {
                    html += `<div class="bash-exit-code ${exitCodeClass}"><strong>Exit code:</strong> <span class="exit-code-value">${parsed.exitCode}</span></div>`;
                }

                // Show output if available
                if (parsed.output) {
                    const ansiRendered = ansiToHtml(parsed.output);
                    html += `<strong>Output:</strong>
                    <div class="bash-output"><pre>${ansiRendered}</pre></div>`;
                } else if (parsed.exitCode !== null) {
                    html += `<div class="bash-no-output"><em>No output</em></div>`;
                }

                html += `</div>`;
            } else {
                html += `<div class="tool-call-result-section"><strong>Result:</strong><pre class="tool-call-result">${escapeHtml(toolCall.result)}</pre></div>`;
            }
        }

        // Show terminal output if this tool call has embedded terminal
        if (toolCall.terminalId) {
            html += `<div class="tool-call-terminal-section">
                <strong>Output:</strong>
                ${renderTerminalOutput(toolCall.terminalId)}
            </div>`;
        }

        html += '</div>';
    }

    html += '</div>';
    return html;
}

// Generate unified diff using Myers' diff algorithm (similar to git diff)
// With syntax highlighting based on file type
function generateUnifiedDiff(oldText, newText, fileName) {
    const oldLines = oldText.split('\n');
    const newLines = newText.split('\n');
    const language = getLanguageFromPath(fileName);

    logToQt('generateUnifiedDiff: fileName=' + fileName + ', language=' + language +
            ', oldLines=' + oldLines.length + ', newLines=' + newLines.length);

    // Compute LCS-based diff using Myers' algorithm
    // Also track original line indices for highlighted lookup
    const diff = computeDiffWithIndices(oldLines, newLines);

    // Pre-highlight both old and new text blocks
    let highlightedOld = oldLines.map(l => escapeHtml(l));
    let highlightedNew = newLines.map(l => escapeHtml(l));

    if (language) {
        try {
            const oldHighlighted = highlightCode(oldText, language);
            const newHighlighted = highlightCode(newText, language);
            logToQt('Diff highlight: oldHighlighted length=' + oldHighlighted.length +
                    ', sample=' + oldHighlighted.substring(0, 200));
            highlightedOld = splitHighlightedLines(oldHighlighted, oldLines.length);
            highlightedNew = splitHighlightedLines(newHighlighted, newLines.length);
            logToQt('Diff highlight: split into ' + highlightedOld.length + ' old lines, ' +
                    highlightedNew.length + ' new lines');
            if (highlightedNew.length > 0) {
                logToQt('First highlighted new line: ' + highlightedNew[0]);
            }
        } catch (e) {
            logToQt('Diff highlight error: ' + e);
            // Fall back to escaped plain text (already set above)
        }
    } else {
        logToQt('Diff highlight: no language detected, using plain text');
    }

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

        // Render hunk with highlighted content
        for (let k = hunkStart; k < hunkEnd; k++) {
            const item = diff[k];
            if (item.type === 'equal') {
                // Context lines - use old index (both are the same content)
                const content = highlightedOld[item.oldIndex] || '';
                result.push(`<span class="diff-context"> ${content}</span>`);
            } else if (item.type === 'delete') {
                const content = highlightedOld[item.oldIndex] || '';
                result.push(`<span class="diff-remove">-${content}</span>`);
            } else if (item.type === 'insert') {
                const content = highlightedNew[item.newIndex] || '';
                result.push(`<span class="diff-add">+${content}</span>`);
            }
        }

        i = hunkEnd;
    }

    return result.join('');
}

// Compute diff using LCS (Longest Common Subsequence) approach
// Returns diff items with original line indices for highlighted lookup
function computeDiffWithIndices(oldLines, newLines) {
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

    // Backtrack to build diff with line indices
    const diff = [];
    let i = n, j = m;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldLines[i - 1] === newLines[j - 1]) {
            diff.unshift({ type: 'equal', value: oldLines[i - 1], oldIndex: i - 1, newIndex: j - 1 });
            i--;
            j--;
        } else if (j > 0 && (i === 0 || lcs[i][j - 1] >= lcs[i - 1][j])) {
            diff.unshift({ type: 'insert', value: newLines[j - 1], newIndex: j - 1 });
            j--;
        } else if (i > 0) {
            diff.unshift({ type: 'delete', value: oldLines[i - 1], oldIndex: i - 1 });
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
    // Also clear todos when messages are cleared (new session)
    clearTodos();
}

// Clear todos display
function clearTodos() {
    const container = document.getElementById('todos-container');
    if (container) {
        container.innerHTML = '';
        container.style.display = 'none';
    }
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
            <span class="todos-toggle">${materialIcon(isCollapsed ? 'expand_less' : 'expand_more', 'material-icon-sm')}</span>
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
            statusIcon = materialIcon('check_circle', 'material-icon-sm');
        } else if (status === 'in_progress') {
            statusIcon = materialIcon('pending', 'material-icon-sm');
        } else {
            statusIcon = materialIcon('radio_button_unchecked', 'material-icon-sm');
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
    toggle.innerHTML = materialIcon(isCollapsed ? 'expand_less' : 'expand_more', 'material-icon-sm');

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
        const originalHTML = button.innerHTML;
        button.innerHTML = '<span class="material-icon material-icon-sm">check</span>';
        button.classList.add('copied');

        setTimeout(() => {
            button.innerHTML = originalHTML;
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

// Show inline permission request (compact single-line style)
function showPermissionRequest(requestId, toolName, input, options) {
    console.log('showPermissionRequest called:', requestId, toolName);
    console.log('Options:', options);

    if (!Array.isArray(options)) {
        console.error('Options is not an array:', options);
        return;
    }

    let html = `
        <div class="permission-request" id="perm-${requestId}">
            <div class="permission-header">
                <span class="permission-icon">${materialIcon('lock', 'material-icon-sm')}</span>
                <span class="permission-title">Permission: ${escapeHtml(toolName)}</span>
            </div>
            <div class="permission-options">
    `;

    options.forEach((option, index) => {
        const label = option.name || option.label || 'Option';
        const optionId = option.optionId || option.id || index.toString();
        html += `<div class="permission-option" onclick="respondToPermission(${requestId}, '${escapeHtml(optionId)}')"><span class="permission-option-label">${escapeHtml(label)}</span></div>`;
    });

    html += `
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

// Terminal support - update terminal output (called from C++ via base64)
function updateTerminal(terminalId, base64Output, finished) {
    // Decode base64 output
    let output;
    try {
        output = decodeURIComponent(escape(atob(base64Output)));
    } catch (e) {
        console.error('Failed to decode terminal output:', e);
        output = '[Decode error]';
    }

    terminals[terminalId] = {
        output: output,
        finished: finished
    };

    logToQt('updateTerminal: ' + terminalId + ' finished=' + finished + ' output=' + output.length + ' chars');

    // Find and update any tool calls with this terminal
    for (const messageId in messages) {
        const msg = messages[messageId];
        if (msg.toolCalls) {
            for (const tc of msg.toolCalls) {
                if (tc.terminalId === terminalId) {
                    // Re-render the message to update terminal display
                    updateMessageDOM(messageId);
                    scrollToBottom();
                    return;
                }
            }
        }
    }
}

// Render terminal output with ANSI color support
function renderTerminalOutput(terminalId) {
    const term = terminals[terminalId];
    if (!term) {
        return '<pre class="terminal-output terminal-waiting">Waiting for output...</pre>';
    }

    const htmlOutput = ansiToHtml(term.output);
    const statusClass = term.finished ? 'terminal-finished' : 'terminal-running';

    return `<pre class="terminal-output ${statusClass}">${htmlOutput}${!term.finished ? '<span class="terminal-indicator">Running...</span>' : ''}</pre>`;
}

// Convert ANSI escape codes to HTML using TerminalRenderer
// This handles cursor positioning, colors, and other escape sequences
function ansiToHtml(text) {
    // Use TerminalRenderer if available (handles cursor positioning)
    if (typeof TerminalRenderer !== 'undefined') {
        try {
            return renderAnsiToHtml(text);
        } catch (e) {
            logToQt('TerminalRenderer error: ' + e);
            // Fall through to simple implementation
        }
    }

    // Fallback: simple color-only processing (no cursor support)
    return ansiToHtmlSimple(text);
}

// Simple ANSI to HTML (colors only, no cursor positioning)
// Used as fallback if TerminalRenderer is not available
function ansiToHtmlSimple(text) {
    // ANSI color code mapping
    const ansiColors = {
        '30': 'ansi-black', '31': 'ansi-red', '32': 'ansi-green',
        '33': 'ansi-yellow', '34': 'ansi-blue', '35': 'ansi-magenta',
        '36': 'ansi-cyan', '37': 'ansi-white',
        '90': 'ansi-bright-black', '91': 'ansi-bright-red',
        '92': 'ansi-bright-green', '93': 'ansi-bright-yellow',
        '94': 'ansi-bright-blue', '95': 'ansi-bright-magenta',
        '96': 'ansi-bright-cyan', '97': 'ansi-bright-white'
    };

    // Background colors
    const ansiBgColors = {
        '40': 'ansi-bg-black', '41': 'ansi-bg-red', '42': 'ansi-bg-green',
        '43': 'ansi-bg-yellow', '44': 'ansi-bg-blue', '45': 'ansi-bg-magenta',
        '46': 'ansi-bg-cyan', '47': 'ansi-bg-white',
        '100': 'ansi-bg-bright-black', '101': 'ansi-bg-bright-red',
        '102': 'ansi-bg-bright-green', '103': 'ansi-bg-bright-yellow',
        '104': 'ansi-bg-bright-blue', '105': 'ansi-bg-bright-magenta',
        '106': 'ansi-bg-bright-cyan', '107': 'ansi-bg-bright-white'
    };

    let result = '';
    let currentClasses = [];

    // Process ANSI codes, then escape text within
    const unescaped = text;
    const regex = /\x1b\[([0-9;]*)m/g;
    let lastIndex = 0;
    let match;

    while ((match = regex.exec(unescaped)) !== null) {
        // Add escaped text before this match
        if (match.index > lastIndex) {
            result += escapeHtml(unescaped.substring(lastIndex, match.index));
        }
        lastIndex = regex.lastIndex;

        const codes = match[1].split(';').filter(c => c !== '');

        for (const code of codes) {
            if (code === '0' || code === '') {
                // Reset - close any open spans
                if (currentClasses.length > 0) {
                    result += '</span>';
                    currentClasses = [];
                }
            } else if (code === '1') {
                // Bold
                if (currentClasses.length > 0) result += '</span>';
                currentClasses.push('ansi-bold');
                result += `<span class="${currentClasses.join(' ')}">`;
            } else if (code === '4') {
                // Underline
                if (currentClasses.length > 0) result += '</span>';
                currentClasses.push('ansi-underline');
                result += `<span class="${currentClasses.join(' ')}">`;
            } else if (ansiColors[code]) {
                // Foreground color
                if (currentClasses.length > 0) result += '</span>';
                // Remove any existing fg color and add new one
                currentClasses = currentClasses.filter(c => !c.startsWith('ansi-') || c.startsWith('ansi-bg-') || c === 'ansi-bold' || c === 'ansi-underline');
                currentClasses.push(ansiColors[code]);
                result += `<span class="${currentClasses.join(' ')}">`;
            } else if (ansiBgColors[code]) {
                // Background color
                if (currentClasses.length > 0) result += '</span>';
                // Remove any existing bg color and add new one
                currentClasses = currentClasses.filter(c => !c.startsWith('ansi-bg-'));
                currentClasses.push(ansiBgColors[code]);
                result += `<span class="${currentClasses.join(' ')}">`;
            }
        }
    }

    // Add any remaining text
    if (lastIndex < unescaped.length) {
        result += escapeHtml(unescaped.substring(lastIndex));
    }

    // Close any remaining open spans
    if (currentClasses.length > 0) {
        result += '</span>';
    }

    return result;
}

// Edit summary state
let trackedEdits = [];

// Update the edit summary panel with tracked edits
function updateEditSummary(editsJson) {
    try {
        trackedEdits = JSON.parse(editsJson);
    } catch (e) {
        logToQt('Failed to parse edits JSON: ' + e);
        return;
    }

    renderEditSummary();
}

// Add a single edit to the summary
function addTrackedEdit(editJson) {
    try {
        const edit = JSON.parse(editJson);
        trackedEdits.push(edit);
        renderEditSummary();
    } catch (e) {
        logToQt('Failed to parse edit JSON: ' + e);
    }
}

// Clear all tracked edits
function clearEditSummary() {
    trackedEdits = [];
    renderEditSummary();
}

// Render the edit summary panel
function renderEditSummary() {
    let panel = document.getElementById('edit-summary-panel');

    // Create panel if it doesn't exist
    if (!panel) {
        panel = document.createElement('div');
        panel.id = 'edit-summary-panel';
        panel.className = 'edit-summary-panel';

        // Append to body (fixed position at bottom)
        document.body.appendChild(panel);
    }

    // Hide panel if no edits
    if (trackedEdits.length === 0) {
        panel.style.display = 'none';
        return;
    }

    panel.style.display = 'block';

    // Build panel HTML
    let html = `
        <div class="edit-summary-header" onclick="toggleEditSummary()">
            <span class="edit-summary-toggle">${materialIcon('expand_more')}</span>
            <span class="edit-summary-title">Edit Summary (${trackedEdits.length} change${trackedEdits.length !== 1 ? 's' : ''})</span>
            <button class="edit-summary-clear" onclick="event.stopPropagation(); clearEditSummary();" title="Clear edit history">
                ${materialIcon('clear_all')}
            </button>
        </div>
        <div class="edit-summary-content" id="edit-summary-content">
    `;

    // Group edits by file
    const editsByFile = {};
    for (const edit of trackedEdits) {
        if (!editsByFile[edit.filePath]) {
            editsByFile[edit.filePath] = [];
        }
        editsByFile[edit.filePath].push(edit);
    }

    // Render each file's edits
    for (const filePath in editsByFile) {
        const fileEdits = editsByFile[filePath];
        const fileName = filePath.split('/').pop();
        const dirPath = filePath.substring(0, filePath.length - fileName.length);

        html += `<div class="edit-file-group">`;
        html += `<div class="edit-file-name" title="${escapeHtml(filePath)}">${escapeHtml(fileName)}</div>`;

        for (const edit of fileEdits) {
            const lineNum = edit.startLine + 1; // Convert to 1-based
            const endLine = edit.startLine + Math.max(edit.oldLineCount, edit.newLineCount);

            let changeText = '';
            if (edit.isNewFile) {
                changeText = `<span class="edit-new-file">created</span> +${edit.newLineCount}`;
            } else if (edit.oldLineCount === -1) {
                // Full file replacement (unknown old line count)
                changeText = `<span class="edit-replaced">replaced</span> ${edit.newLineCount} lines`;
            } else {
                const added = edit.newLineCount;
                const removed = edit.oldLineCount;
                if (added > 0 && removed > 0) {
                    changeText = `<span class="edit-added">+${added}</span>/<span class="edit-removed">-${removed}</span>`;
                } else if (added > 0) {
                    changeText = `<span class="edit-added">+${added}</span>`;
                } else if (removed > 0) {
                    changeText = `<span class="edit-removed">-${removed}</span>`;
                } else {
                    changeText = '<span class="edit-unchanged">unchanged</span>';
                }
            }

            // Escape filePath for use in JavaScript string literal within HTML attribute
            const escapedPath = filePath.replace(/\\/g, '\\\\').replace(/'/g, "\\'");
            html += `
                <div class="edit-entry" onclick="jumpToEdit('${escapedPath}', ${edit.startLine}, ${endLine})">
                    <span class="edit-line">L${lineNum}</span>
                    <span class="edit-changes">${changeText}</span>
                </div>
            `;
        }

        html += `</div>`;
    }

    html += `</div>`;
    panel.innerHTML = html;
}

// Toggle edit summary panel collapsed state
function toggleEditSummary() {
    const content = document.getElementById('edit-summary-content');
    const panel = document.getElementById('edit-summary-panel');
    if (content && panel) {
        panel.classList.toggle('collapsed');
    }
}

// Jump to an edit location - calls back to Qt
function jumpToEdit(filePath, startLine, endLine) {
    logToQt('jumpToEdit called: ' + filePath + ' lines ' + startLine + '-' + endLine);
    if (bridge) {
        logToQt('Calling bridge.jumpToEdit...');
        bridge.jumpToEdit(filePath, startLine, endLine);
    } else {
        logToQt('Bridge not available for jumpToEdit');
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
window.updateTerminal = updateTerminal;
window.updateEditSummary = updateEditSummary;
window.addTrackedEdit = addTrackedEdit;
window.clearEditSummary = clearEditSummary;
window.toggleEditSummary = toggleEditSummary;
window.jumpToEdit = jumpToEdit;
