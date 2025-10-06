const TOKEN_KEY = "mail.token";
const USER_KEY = "mail.user";

function setMessage(text, isError = false) {
    const el = document.getElementById("login-message");
    if (!el) return;
    el.textContent = text || "";
    el.style.color = isError ? "#dc2626" : "#16a34a";
}

async function login(username, password) {
    const resp = await fetch("/api/login", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify({ username, password })
    });

    const text = await resp.text();
    let payload = null;
    if (text) {
        try {
            payload = JSON.parse(text);
        } catch (_) {
            // ignore parse error
        }
    }

    if (!resp.ok) {
        const message = payload?.error?.message || "无法登录，请检查用户名或密码";
        throw new Error(message);
    }
    return payload;
}

async function registerAccount(username, email, password) {
    const resp = await fetch("/api/register", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify({ username, email, password })
    });

    const text = await resp.text();
    let payload = null;
    if (text) {
        try {
            payload = JSON.parse(text);
        } catch (_) {
            // ignore parse error
        }
    }

    if (!resp.ok) {
        const message = payload?.error?.message || "无法创建账号";
        throw new Error(message);
    }
    return payload;
}

document.addEventListener("DOMContentLoaded", () => {
    if (localStorage.getItem(TOKEN_KEY)) {
        window.location.href = "/app";
        return;
    }

    const loginForm = document.getElementById("login-form");
    const registerForm = document.getElementById("register-form");
    const toggleButtons = document.querySelectorAll("[data-auth-mode]");
    const authPanels = document.querySelectorAll("[data-auth-panel]");
    if (!loginForm || !registerForm) return;

    const switchMode = (mode) => {
        toggleButtons.forEach((btn) => {
            const isActive = btn.getAttribute("data-auth-mode") === mode;
            btn.classList.toggle("is-active", isActive);
            btn.setAttribute("aria-selected", isActive ? "true" : "false");
        });
        authPanels.forEach((panel) => {
            const isActive = panel.getAttribute("data-auth-panel") === mode;
            panel.classList.toggle("is-hidden", !isActive);
            panel.setAttribute("aria-hidden", isActive ? "false" : "true");
        });
        setMessage("");
        const form = mode === "login" ? loginForm : registerForm;
        const firstField = form.querySelector("input[name='username']");
        if (firstField) {
            firstField.focus();
        }
    };

    toggleButtons.forEach((btn) => {
        btn.addEventListener("click", () => {
            const mode = btn.getAttribute("data-auth-mode");
            if (mode) {
                switchMode(mode);
            }
        });
    });

    switchMode("login");

    loginForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        setMessage("正在登录…");
        const data = new FormData(loginForm);
        const username = data.get("username")?.trim();
        const password = data.get("password")?.trim();

        if (!username || !password) {
            setMessage("请填写用户名和密码", true);
            return;
        }
        try {
            const result = await login(username, password);
            localStorage.setItem(TOKEN_KEY, result.token);
            if (result.user) {
                localStorage.setItem(USER_KEY, JSON.stringify(result.user));
            }
            setMessage("登录成功，正在跳转…");
            setTimeout(() => {
                window.location.href = "/app";
            }, 450);
        } catch (err) {
            console.error("login failed", err);
            setMessage(err.message || "无法登录", true);
        }
    });

    registerForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        setMessage("正在创建账号…");
        const data = new FormData(registerForm);
        const username = data.get("username")?.trim();
        const email = data.get("email")?.trim();
        const password = data.get("password")?.trim();
        const confirm = data.get("confirm")?.trim();

        if (!username || !email || !password || !confirm) {
            setMessage("请完整填写所有字段", true);
            return;
        }
        if (password !== confirm) {
            setMessage("两次输入的密码不一致", true);
            return;
        }

        try {
            const result = await registerAccount(username, email, password);
            localStorage.setItem(TOKEN_KEY, result.token);
            if (result.user) {
                localStorage.setItem(USER_KEY, JSON.stringify(result.user));
            }
            setMessage("注册成功，正在进入邮箱…");
            setTimeout(() => {
                window.location.href = "/app";
            }, 450);
        } catch (err) {
            console.error("registration failed", err);
            setMessage(err.message || "无法创建账号", true);
        }
    });
});
