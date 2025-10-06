const TOKEN_KEY = "mail.token";
const USER_KEY = "mail.user";

const FOLDER_LABELS = {
    inbox: "收件箱",
    sent: "已发送",
    drafts: "草稿箱",
    starred: "星标邮件",
    archive: "归档",
    custom: "自定义文件夹"
};

const MESSAGE_EMPTY_TEXT = "暂无邮件";
const NO_SUBJECT_TEXT = "（无主题）";
const DEFAULT_ARCHIVE_GROUP = "未分组";

const state = {
    token: null,
    user: null,
    folders: [],
    messages: [],
    selectedFolder: null,
    selectedMessage: null,
    attachments: [],
    composeAttachments: [],
    archiveGroups: [],
    contacts: [],
    contactsLoaded: false,
    pendingArchive: null
};

const elements = {};

function cacheElements() {
    elements.userEmail = document.getElementById("user-email");
    elements.folderList = document.getElementById("folder-list");
    elements.messageList = document.getElementById("message-list");
    elements.currentFolderName = document.getElementById("current-folder-name");
    elements.detailSubject = document.getElementById("detail-subject");
    elements.detailRecipients = document.getElementById("detail-recipients");
    elements.detailDate = document.getElementById("detail-date");
    elements.detailArchiveGroup = document.getElementById("detail-archive-group");
    elements.detailBody = document.getElementById("detail-body");
    elements.attachmentList = document.getElementById("attachment-list");
    elements.starToggle = document.getElementById("star-toggle");
    elements.archiveToggle = document.getElementById("archive-toggle");
    elements.refreshButton = document.getElementById("refresh-button");
    elements.logoutButton = document.getElementById("logout-button");
    elements.composeButton = document.getElementById("compose-button");
    elements.composeModal = document.getElementById("compose-modal");
    elements.composeBackdrop = document.getElementById("compose-backdrop");
    elements.composeForm = document.getElementById("compose-form");
    elements.composeClose = document.getElementById("compose-close");
    elements.saveDraft = document.getElementById("save-draft");
    elements.composeMessage = document.getElementById("compose-message");
    elements.composeAttachmentList = document.getElementById("compose-attachment-list");
    elements.attachFilesBtn = document.getElementById("attach-files-btn");
    elements.attachFolderBtn = document.getElementById("attach-folder-btn");
    elements.attachFilesInput = document.getElementById("attach-files-input");
    elements.attachFolderInput = document.getElementById("attach-folder-input");
    elements.archiveModal = document.getElementById("archive-modal");
    elements.archiveBackdrop = document.getElementById("archive-backdrop");
    elements.archiveForm = document.getElementById("archive-form");
    elements.archiveClose = document.getElementById("archive-close");
    elements.archiveCancel = document.getElementById("archive-cancel");
    elements.archiveGroupInput = document.getElementById("archive-group-input");
    elements.archiveSuggestions = document.getElementById("archive-group-suggestions");
    elements.contactsButton = document.getElementById("contacts-button");
    elements.contactsModal = document.getElementById("contacts-modal");
    elements.contactsBackdrop = document.getElementById("contacts-backdrop");
    elements.contactsClose = document.getElementById("contacts-close");
    elements.contactsGroupList = document.getElementById("contacts-group-list");
    elements.contactForm = document.getElementById("contact-form");
    elements.contactMessage = document.getElementById("contact-message");
    elements.toast = document.getElementById("toast");
}

function hydrateFromStorage() {
    state.token = localStorage.getItem(TOKEN_KEY);
    if (!state.token) {
        window.location.href = "/";
        return false;
    }
    try {
        const raw = localStorage.getItem(USER_KEY);
        if (raw) {
            state.user = JSON.parse(raw);
        }
    } catch (err) {
        console.warn("Failed to parse stored user", err);
    }
    return true;
}

function handleUnauthorized() {
    localStorage.removeItem(TOKEN_KEY);
    localStorage.removeItem(USER_KEY);
    window.location.href = "/";
}

async function api(path, options = {}) {
    const init = { ...options };
    init.method = init.method || "GET";
    const headers = new Headers(init.headers || {});
    if (state.token) {
        headers.set("Authorization", `Bearer ${state.token}`);
    }
    let body = init.body;
    const shouldStringify = body && typeof body === "object" && !(body instanceof FormData) && !(body instanceof Blob);
    if (shouldStringify) {
        headers.set("Content-Type", "application/json");
        body = JSON.stringify(body);
    }
    init.headers = headers;
    if (body !== undefined) {
        init.body = body;
    }

    const response = await fetch(path, init);
    const contentType = response.headers.get("Content-Type") || "";
    let payload = null;
    if (contentType.includes("application/json")) {
        try {
            payload = await response.json();
        } catch (err) {
            payload = null;
        }
    } else {
        const text = await response.text();
        payload = text ? { raw: text } : null;
    }

    if (response.status === 401) {
        handleUnauthorized();
        throw new Error("登录状态已过期");
    }

    if (!response.ok) {
        const message = payload?.error?.message || payload?.message || response.statusText || "请求失败";
        const error = new Error(message);
        error.status = response.status;
        error.payload = payload;
        throw error;
    }
    return payload || {};
}

function showToast(message, type = "success") {
    if (!elements.toast) return;
    elements.toast.textContent = message;
    elements.toast.classList.remove("hidden", "success", "error");
    elements.toast.classList.add(type === "error" ? "error" : "success");
    clearTimeout(elements.toast._timer);
    elements.toast._timer = setTimeout(() => {
        elements.toast.classList.add("hidden");
    }, 3200);
}

function clearToast() {
    if (!elements.toast) return;
    elements.toast.textContent = "";
    elements.toast.classList.add("hidden");
}

function folderDisplayName(folder) {
    if (!folder) return "";
    if (folder.kind === "custom") {
        return folder.name || FOLDER_LABELS.custom;
    }
    return FOLDER_LABELS[folder.kind] || folder.name || "";
}

function folderKindLabel(kind, customName) {
    if (kind === "custom") return customName || FOLDER_LABELS.custom;
    return FOLDER_LABELS[kind] || kind;
}

function collectArchiveGroups() {
    const seen = new Set();
    state.messages.forEach((msg) => {
        if (msg.archiveGroup) {
            seen.add(msg.archiveGroup);
        }
    });
    if (state.selectedMessage?.archiveGroup) {
        seen.add(state.selectedMessage.archiveGroup);
    }
    state.archiveGroups = Array.from(seen).filter(Boolean).sort((a, b) => a.localeCompare(b, "zh-CN"));
    updateArchiveSuggestions();
}

function updateArchiveSuggestions() {
    if (!elements.archiveSuggestions) return;
    elements.archiveSuggestions.innerHTML = "";
    state.archiveGroups.forEach((group) => {
        const option = document.createElement("option");
        option.value = group;
        elements.archiveSuggestions.appendChild(option);
    });
}

function bytesToSize(bytes) {
    if (!Number.isFinite(bytes) || bytes < 0) return "";
    const units = ["B", "KB", "MB", "GB", "TB"];
    let size = bytes;
    let unitIndex = 0;
    while (size >= 1024 && unitIndex < units.length - 1) {
        size /= 1024;
        unitIndex++;
    }
    const precision = size < 10 && unitIndex > 0 ? 1 : 0;
    return `${size.toFixed(precision)}${units[unitIndex]}`;
}

function fileToBase64(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => {
            const result = typeof reader.result === "string" ? reader.result : "";
            const base64 = result.includes(",") ? result.split(",", 2)[1] : result;
            resolve(base64 || "");
        };
        reader.onerror = () => reject(reader.error || new Error("无法读取文件"));
        reader.readAsDataURL(file);
    });
}

async function addAttachmentsFromFiles(fileList) {
    if (!fileList || fileList.length === 0) return;
    const files = Array.from(fileList);
    try {
        const additions = await Promise.all(files.map(async (file) => {
            const base64 = await fileToBase64(file);
            const relativePath = file.webkitRelativePath || file.relativePath || file.name;
            return {
                filename: file.name,
                mimeType: file.type || "application/octet-stream",
                relativePath,
                base64,
                size: file.size
            };
        }));
        additions.forEach((att) => {
            const idx = state.composeAttachments.findIndex((item) => item.relativePath === att.relativePath && item.filename === att.filename);
            if (idx >= 0) {
                state.composeAttachments[idx] = att;
            } else {
                state.composeAttachments.push(att);
            }
        });
        renderComposeAttachments();
    } catch (err) {
        console.error("附件读取失败", err);
        showToast(err.message || "无法读取附件", "error");
    } finally {
        if (elements.attachFilesInput) elements.attachFilesInput.value = "";
        if (elements.attachFolderInput) elements.attachFolderInput.value = "";
    }
}

function removeComposeAttachment(index) {
    state.composeAttachments.splice(index, 1);
    renderComposeAttachments();
}

function renderComposeAttachments() {
    if (!elements.composeAttachmentList) return;
    elements.composeAttachmentList.innerHTML = "";
    if (!state.composeAttachments.length) {
        const empty = document.createElement("li");
        empty.className = "compose-attachment-item";
        empty.textContent = "尚未添加附件";
        elements.composeAttachmentList.appendChild(empty);
        return;
    }
    state.composeAttachments.forEach((att, index) => {
        const li = document.createElement("li");
        li.className = "compose-attachment-item";

        const path = document.createElement("span");
        path.className = "attachment-path";
        path.textContent = att.relativePath || att.filename;

        const meta = document.createElement("span");
        meta.className = "attachment-meta";
        meta.textContent = bytesToSize(att.size);

        const remove = document.createElement("button");
        remove.type = "button";
        remove.className = "attachment-remove";
        remove.textContent = "移除";
        remove.addEventListener("click", () => removeComposeAttachment(index));

        li.append(path, meta, remove);
        elements.composeAttachmentList.appendChild(li);
    });
}

function renderUser() {
    if (elements.userEmail && state.user) {
        const display = state.user.email || state.user.username || "";
        elements.userEmail.textContent = display;
    }
}

function renderFolders() {
    if (!elements.folderList) return;
    elements.folderList.innerHTML = "";
    const fragment = document.createDocumentFragment();
    state.folders.forEach((folder) => {
        const li = document.createElement("li");
        const button = document.createElement("button");
        button.textContent = folderDisplayName(folder);
        button.dataset.kind = folder.kind;
        if (folder.kind === "custom") {
            button.dataset.custom = folder.name;
        }
        const isActive =
            state.selectedFolder &&
            state.selectedFolder.kind === folder.kind &&
            (folder.kind !== "custom" || state.selectedFolder.custom === folder.name);
        if (isActive) {
            button.classList.add("active");
        }
        button.addEventListener("click", () => selectFolder(folder));
        li.appendChild(button);
        fragment.appendChild(li);
    });
    elements.folderList.appendChild(fragment);
}

function formatTimestamp(unixSeconds) {
    if (!unixSeconds) return "";
    const date = new Date(unixSeconds * 1000);
    return date.toLocaleString(undefined, {
        hour: "2-digit",
        minute: "2-digit",
        month: "short",
        day: "numeric"
    });
}

function formatPreview(text) {
    if (!text) return "";
    const trimmed = text.replace(/\s+/g, " ").trim();
    return trimmed.length > 120 ? `${trimmed.slice(0, 117)}…` : trimmed;
}

function renderMessages() {
    if (!elements.messageList) return;
    elements.messageList.innerHTML = "";
    if (!state.messages.length) {
        const empty = document.createElement("li");
        empty.className = "message-item";
        empty.textContent = MESSAGE_EMPTY_TEXT;
        elements.messageList.appendChild(empty);
        return;
    }
    const fragment = document.createDocumentFragment();
    state.messages.forEach((msg) => {
        const li = document.createElement("li");
        li.className = "message-item";
        if (state.selectedMessage && state.selectedMessage.id === msg.id) {
            li.classList.add("active");
        }
        li.dataset.id = msg.id;

        const subject = document.createElement("div");
        subject.className = "subject";
        subject.textContent = msg.subject || NO_SUBJECT_TEXT;

        const preview = document.createElement("div");
        preview.className = "preview";
        preview.textContent = formatPreview(msg.body);

        const meta = document.createElement("div");
        meta.className = "meta";
        const folderLabel = folderKindLabel(msg.folder, msg.customFolder);
        meta.innerHTML = `<span>${folderLabel}</span><span>${formatTimestamp(msg.updatedAt || msg.createdAt)}</span>`;

        li.append(subject, preview, meta);
        li.addEventListener("click", () => openMessage(msg.id));
        fragment.appendChild(li);
    });
    elements.messageList.appendChild(fragment);
}

function clearMessageDetail() {
    if (elements.detailSubject) elements.detailSubject.textContent = "Select a message";
    if (elements.detailRecipients) elements.detailRecipients.textContent = "";
    if (elements.detailDate) elements.detailDate.textContent = "";
    if (elements.detailBody) elements.detailBody.textContent = "";
    if (elements.attachmentList) elements.attachmentList.innerHTML = "";
    if (elements.starToggle) elements.starToggle.disabled = true;
    if (elements.archiveToggle) elements.archiveToggle.disabled = true;
}

function renderMessageDetail() {
    if (!state.selectedMessage) {
        clearMessageDetail();
        return;
    }
    const msg = state.selectedMessage;
    if (elements.detailSubject) elements.detailSubject.textContent = msg.subject || "(No subject)";
    if (elements.detailRecipients) elements.detailRecipients.textContent = msg.recipients || "";
    if (elements.detailDate) elements.detailDate.textContent = formatTimestamp(msg.updatedAt || msg.createdAt);
    if (elements.detailBody) elements.detailBody.textContent = msg.body || "";

    if (elements.attachmentList) {
        elements.attachmentList.innerHTML = "";
        if (state.attachments && state.attachments.length) {
            state.attachments.forEach((att) => {
                const pill = document.createElement("span");
                pill.className = "attachment-pill";
                pill.textContent = att.filename;
                elements.attachmentList.appendChild(pill);
            });
        }
    }

    if (elements.starToggle) {
        elements.starToggle.disabled = false;
        elements.starToggle.textContent = msg.isStarred ? "Unstar" : "Star";
    }
    if (elements.archiveToggle) {
        elements.archiveToggle.disabled = false;
        elements.archiveToggle.textContent = msg.isArchived ? "Unarchive" : "Archive";
    }
}

async function openMessage(messageId) {
    try {
        const data = await api(`/api/messages/${messageId}`);
        state.selectedMessage = data.message;
        state.attachments = data.attachments || [];
        renderMessages();
        renderMessageDetail();
    } catch (err) {
        console.error("Failed to open message", err);
        showToast(err.message || "Unable to load message", "error");
    }
}

async function toggleStar() {
    if (!state.selectedMessage) return;
    const target = !state.selectedMessage.isStarred;
    try {
        await api(`/api/messages/${state.selectedMessage.id}/star`, {
            method: "POST",
            body: { starred: target }
        });
        state.selectedMessage.isStarred = target;
        showToast(target ? "Message starred" : "Star removed");
        renderMessageDetail();
    } catch (err) {
        showToast(err.message || "Unable to update star", "error");
    }
}

async function toggleArchive() {
    if (!state.selectedMessage) return;
    const target = !state.selectedMessage.isArchived;
    try {
        await api(`/api/messages/${state.selectedMessage.id}/archive`, {
            method: "POST",
            body: { archived: target }
        });
        state.selectedMessage.isArchived = target;
        showToast(target ? "Message archived" : "Message restored");
        renderMessageDetail();
        await loadMessages();
    } catch (err) {
        showToast(err.message || "Unable to update archive", "error");
    }
}

function openCompose() {
    if (!elements.composeModal) return;
    elements.composeModal.classList.remove("hidden");
    elements.composeMessage.textContent = "";
    const recipients = elements.composeForm?.elements?.recipients;
    if (recipients) recipients.focus();
}

function closeCompose() {
    if (!elements.composeModal) return;
    elements.composeModal.classList.add("hidden");
    if (elements.composeForm) {
        elements.composeForm.reset();
    }
    elements.composeMessage.textContent = "";
}

async function submitCompose(saveAsDraft = false) {
    if (!elements.composeForm) return;
    const data = new FormData(elements.composeForm);
    const payload = {
        recipients: data.get("recipients")?.toString().trim(),
        subject: data.get("subject")?.toString().trim() || "",
        body: data.get("body")?.toString() || "",
        saveAsDraft
    };

    if (!payload.recipients) {
        elements.composeMessage.textContent = "Recipients are required";
        return;
    }
    elements.composeMessage.textContent = saveAsDraft ? "Saving draft..." : "Sending...";

    try {
        await api("/api/messages", {
            method: "POST",
            body: payload
        });
        elements.composeMessage.textContent = saveAsDraft ? "Draft saved" : "Message sent";
        showToast(saveAsDraft ? "Draft saved" : "Message sent");
        if (!saveAsDraft) {
            closeCompose();
            await loadMessages();
        }
    } catch (err) {
        console.error("compose failed", err);
        elements.composeMessage.textContent = err.message || "Unable to send";
    }
}

async function loadSession() {
    try {
        const data = await api("/api/session");
        state.user = data.user;
        if (state.user) {
            localStorage.setItem(USER_KEY, JSON.stringify(state.user));
        }
        renderUser();
    } catch (err) {
        console.error("session check failed", err);
        showToast(err.message || "Unable to load session", "error");
    }
}

async function loadFolders() {
    try {
        const data = await api("/api/mailboxes");
        state.folders = data.folders || [];
        if (!state.selectedFolder && state.folders.length) {
            state.selectedFolder = {
                kind: state.folders[0].kind,
                custom: state.folders[0].kind === "custom" ? state.folders[0].name : null,
                name: state.folders[0].name
            };
        }
        renderFolders();
    } catch (err) {
        console.error("failed to load folders", err);
        showToast(err.message || "Unable to load folders", "error");
    }
}

async function loadMessages() {
    if (!state.selectedFolder) return;
    const params = new URLSearchParams();
    params.set("folder", state.selectedFolder.kind);
    if (state.selectedFolder.kind === "custom" && state.selectedFolder.custom) {
        params.set("custom", state.selectedFolder.custom);
    }
    try {
        const data = await api(`/api/messages?${params.toString()}`);
        state.messages = data.messages || [];
        if (!state.messages.find((m) => m.id === state.selectedMessage?.id)) {
            state.selectedMessage = null;
            state.attachments = [];
            clearMessageDetail();
        }
        renderMessages();
        if (elements.currentFolderName) {
            elements.currentFolderName.textContent = state.selectedFolder.name || state.selectedFolder.kind;
        }
    } catch (err) {
        console.error("failed to load messages", err);
        showToast(err.message || "Unable to load messages", "error");
    }
}

function selectFolder(folder) {
    state.selectedFolder = {
        kind: folder.kind,
        custom: folder.kind === "custom" ? folder.name : null,
        name: folder.name
    };
    state.selectedMessage = null;
    state.attachments = [];
    renderFolders();
    clearMessageDetail();
    loadMessages();
}

function registerEventListeners() {
    elements.logoutButton?.addEventListener("click", async () => {
        try {
            await api("/api/logout", { method: "POST" });
        } catch (err) {
            console.warn("logout error", err);
        }
        handleUnauthorized();
    });
    elements.refreshButton?.addEventListener("click", () => {
        loadMessages();
    });
    elements.composeButton?.addEventListener("click", () => openCompose());
    elements.composeClose?.addEventListener("click", () => closeCompose());
    elements.composeBackdrop?.addEventListener("click", () => closeCompose());
    elements.starToggle?.addEventListener("click", () => toggleStar());
    elements.archiveToggle?.addEventListener("click", () => toggleArchive());
    elements.saveDraft?.addEventListener("click", (ev) => {
        ev.preventDefault();
        submitCompose(true);
    });
    elements.composeForm?.addEventListener("submit", (ev) => {
        ev.preventDefault();
        submitCompose(false);
    });
    document.addEventListener("keydown", (ev) => {
        if (ev.key === "Escape" && !elements.composeModal?.classList.contains("hidden")) {
            closeCompose();
        }
    });
}

async function bootstrap() {
    renderUser();
    await loadSession();
    await loadFolders();
    await loadMessages();
}

document.addEventListener("DOMContentLoaded", () => {
    if (!hydrateFromStorage()) return;
    cacheElements();
    registerEventListeners();
    bootstrap();
});
