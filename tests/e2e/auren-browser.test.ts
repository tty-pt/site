import { chromium } from "npm:playwright";

const BASE = "http://localhost:8080";

Deno.test("auren browser demo: verify SSR, WebAssembly client hydration, and prompt generation", async () => {
  const browser = await chromium.launch({
    args: ["--no-sandbox", "--disable-setuid-sandbox"]
  });
  const context = await browser.newContext();
  const page = await context.newPage();

  const consoleLogs: string[] = [];
  page.on("console", (msg) => consoleLogs.push(`[${msg.type()}] ${msg.text()}`));

  try {
    /* 1. Navigate to /auren */
    const res = await page.goto(`${BASE}/auren`, { waitUntil: "networkidle" });
    if (!res || res.status() !== 200) {
      throw new Error(`/auren navigation failed with status ${res?.status()}`);
    }

    /* 2. Verify SSR & DOM structure */
    await page.waitForSelector(".auren-container", { timeout: 5000 });
    const titleEl = await page.$(".auren-title-row h1");
    if (!titleEl) throw new Error("Auren title element missing");
    const titleText = await titleEl.textContent();
    if (!titleText?.includes("Auren In-Browser LLM Demo")) {
      throw new Error(`Unexpected title: ${titleText}`);
    }

    /* 3. Verify Hardware / Status Badge */
    const badgeEl = await page.$(".auren-badge");
    if (!badgeEl) throw new Error("Hardware status badge missing");

    /* 4. Verify input bar and quick prompts */
    const formEl = await page.$("#auren-form");
    const inputEl = await page.$("#auren-input");
    const submitBtn = await page.$("#auren-submit-btn");
    if (!formEl || !inputEl || !submitBtn) {
      throw new Error("Input bar elements missing");
    }

    /* 5. Submit a prompt and verify streaming in the browser */
    await inputEl.fill("Explain pointers in C in 5 words.");
    await submitBtn.click();

    /* 6. Verify user message appears in viewport */
    await page.waitForSelector(".auren-msg.msg-user", { timeout: 3000 });
    const userMsg = await page.$(".auren-msg.msg-user .auren-msg-body");
    const userText = await userMsg?.textContent();
    if (!userText?.includes("Explain pointers in C in 5 words.")) {
      throw new Error(`User message mismatch: ${userText}`);
    }

    /* 7. Verify assistant message streams response tokens */
    await page.waitForSelector(".auren-msg.msg-assistant", { timeout: 5000 });
    await page.waitForFunction(
      () => {
        const el = document.querySelector(".auren-msg.msg-assistant .auren-msg-body");
        return el && el.textContent && el.textContent.trim().length > 0;
      },
      { timeout: 8000 }
    );

    const assistantMsg = await page.$(".auren-msg.msg-assistant .auren-msg-body");
    const assistantText = await assistantMsg?.textContent();
    if (!assistantText || assistantText.trim().length === 0) {
      throw new Error("Assistant response was empty");
    }

    console.log("Verified browser LLM response:", assistantText.trim());

    /* 8. Test quick prompt chip click */
    const quickChip = await page.$(".btn-chip[data-prompt*='Isomorphic']");
    if (quickChip) {
      await quickChip.click();
      await page.waitForTimeout(600);
      const allMsgs = await page.$$(".auren-msg");
      if (allMsgs.length < 3) {
        throw new Error("Quick prompt did not append new messages");
      }
    }

  } finally {
    await browser.close();
  }
});
