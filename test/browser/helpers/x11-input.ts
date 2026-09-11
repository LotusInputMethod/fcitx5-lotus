import { expect } from '@playwright/test';
import type { Page, Locator } from '@playwright/test';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { getEventLog } from './events';

const execFileAsync = promisify(execFile);

/**
 * Executes `xdotool key --delay <delayMs> <keys...>` to inject X11 XTEST key events.
 */
export async function typeXdotool(
  keys: string | string[],
  delayMs = 60
): Promise<void> {
  const keyList = Array.isArray(keys) ? keys : [keys];
  if (keyList.length === 0) {
    return;
  }

  // Normalize common key names for xdotool
  const normalizedKeys = keyList.map((k) => (k === ' ' ? 'space' : k));

  await execFileAsync('xdotool', [
    'key',
    '--delay',
    String(delayMs),
    ...normalizedKeys,
  ]);
}

/**
 * Returns the active X11 window ID and title via xdotool.
 */
export async function getActiveX11Window(): Promise<{ id: string; name: string }> {
  try {
    const { stdout: idOut } = await execFileAsync('xdotool', ['getactivewindow']);
    const id = idOut.trim();
    const { stdout: nameOut } = await execFileAsync('xdotool', ['getwindowname', id]).catch(() => ({ stdout: '' }));
    return { id, name: nameOut.trim() };
  } catch {
    return { id: '', name: '' };
  }
}

/**
 * Ensures the target locator is clicked, focused, and waits until the X11
 * window is active and the browser has processed the focus (IM context ready).
 */
export async function ensureActive(
  page: Page,
  locator: Locator
): Promise<void> {
  await page.bringToFront();
  await locator.click();
  await expect(locator).toBeFocused();

  // Verify that the active X11 window belongs to the browser fixture
  await expect
    .poll(
      async () => {
        const win = await getActiveX11Window();
        return win.name;
      },
      { timeout: 2000 }
    )
    .toContain('Fcitx5 Lotus Browser E2E Fixture');

  // Poll the fixture event log until the browser processed the X11 focus and
  // created the input context: a focus event must be recorded for this element.
  const targetId = await locator.evaluate((el: HTMLElement) => el.id);
  await expect
    .poll(
      async () =>
        (await getEventLog(page)).some(
          (e) => e.type === 'focus' && e.targetId === targetId
        ),
      { timeout: 2000 }
    )
    .toBe(true);
}

/**
 * Clears an input, textarea, or contenteditable element using X11 select-all and backspace.
 */
export async function clearInput(
  page: Page,
  locator: Locator
): Promise<void> {
  await ensureActive(page, locator);
  await typeXdotool('ctrl+a', 50);
  await typeXdotool('BackSpace', 50);
  await expect
    .poll(async () => {
      return await locator.evaluate((el: HTMLElement) => {
        if ('value' in el && typeof (el as HTMLInputElement).value === 'string') {
          return (el as HTMLInputElement).value;
        }
        return (el.textContent || '').trim();
      });
    }, { timeout: 2000 })
    .toBe('');
}

/**
 * Direct DOM reset for fixture initialization outside of input method testing.
 * MUST NOT be used as a fallback for user-level keyboard interactions.
 */
export async function resetFixtureDirectly(locator: Locator): Promise<void> {
  await locator.evaluate((el: HTMLElement) => {
    if ('value' in el && typeof (el as HTMLInputElement).value === 'string') {
      (el as HTMLInputElement).value = '';
    } else {
      el.textContent = '';
    }
    el.dispatchEvent(new Event('input', { bubbles: true }));
  });
}

/**
 * Focuses locator and types the given sequence of keys through xdotool.
 */
export async function typeWithLotus(
  page: Page,
  locator: Locator,
  keys: string[],
  delayMs = 60
): Promise<void> {
  await ensureActive(page, locator);
  await typeXdotool(keys, delayMs);
}
