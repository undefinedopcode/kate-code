// Chat state
let messages = {};
let bridge = null;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    console.log('Chat UI initialized');

    // Configure marked.js with syntax highlighting
    if (typeof marked !== 'undefined') {
        marked.setOptions({
            breaks: true,
            gfm: true,
            highlight: function(code, lang) {
                if (typeof hljs !== 'undefined') {
                    if (lang && hljs.getLanguage(lang)) {
                        try {
                            return hljs.highlight(code, { language: lang }).value;
                        } catch (e) {
                            console.error('Highlight error:', e);
                        }
                    }
                    // Auto-detect language if not specified
                    try {
                        return hljs.highlightAuto(code).value;
                    } catch (e) {
                        console.error('Highlight auto error:', e);
                    }
                }
                return code;
            }
        });
    }

    // Setup Qt WebChannel if available
    if (typeof QWebChannel !== 'undefined' && typeof qt !== 'undefined') {
        new QWebChannel(qt.webChannelTransport, (channel) => {
            window.bridge = channel.objects.bridge;
            console.log('Qt WebChannel bridge connected:', window.bridge);
        });
    } else {
        console.warn('QWebChannel or qt not available');
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
function addToolCall(messageId, toolCallId, name, status, filePath, inputJson) {
    if (!messages[messageId]) return;

    // Parse input to extract command for Bash tools
    let input = {};
    try {
        input = inputJson ? JSON.parse(inputJson) : {};
    } catch (e) {
        console.warn('Failed to parse tool input JSON:', e);
    }

    // Check if tool call already exists (avoid duplicates)
    const existing = messages[messageId].toolCalls.find(tc => tc.id === toolCallId);
    if (existing) {
        // Update existing tool call
        existing.status = status;
        existing.name = name || existing.name;
        existing.filePath = filePath || existing.filePath;
        existing.input = input;
    } else {
        // Add new tool call at current content length position
        const toolCall = {
            id: toolCallId,
            name: name,
            status: status,
            filePath: filePath || '',
            input: input,
            result: '',
            position: messages[messageId].content.length
        };
        messages[messageId].toolCalls.push(toolCall);
    }

    updateMessageDOM(messageId);
    scrollToBottom();
}

// Update tool call status
function updateToolCall(messageId, toolCallId, status, result) {
    if (!messages[messageId]) return;

    const toolCall = messages[messageId].toolCalls.find(tc => tc.id === toolCallId);
    if (!toolCall) return;

    toolCall.status = status;
    toolCall.result = result || '';

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

        // Show result if available
        if (toolCall.result) {
            html += `<div class="tool-call-result-section"><strong>Result:</strong><pre class="tool-call-result">${escapeHtml(toolCall.result)}</pre></div>`;
        }

        html += '</div>';
    }

    html += '</div>';
    return html;
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

// Show inline permission request
function showPermissionRequest(requestId, toolName, input, options) {
    console.log('showPermissionRequest called:', requestId, toolName);
    console.log('Options:', options);

    if (!Array.isArray(options)) {
        console.error('Options is not an array:', options);
        return;
    }

    // Convert input object to formatted JSON string for display
    const inputDisplay = typeof input === 'object'
        ? JSON.stringify(input, null, 2)
        : String(input);

    let html = `
        <div class="permission-request" id="perm-${requestId}">
            <div class="permission-header">
                <span class="permission-icon">🔐</span>
                <span class="permission-title">Permission Required: ${escapeHtml(toolName)}</span>
            </div>
            <div class="permission-body">
                <div class="permission-details">
                    <pre>${escapeHtml(inputDisplay)}</pre>
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
