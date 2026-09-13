import { test, expect } from '@playwright/test';
import { clearInput, ensureActive, typeWithLotus } from '../helpers/x11-input';
import { getActiveIM, switchIM, activateIM } from '../helpers/fcitx5';
import { resetEventLog, getEventLog, attachEventLog } from '../helpers/events';

test.describe('Fcitx5 Lotus Control Tests', () => {
  test.beforeEach(async ({ page }) => {
    await activateIM();
    await page.goto('/');
  });

  test.afterEach(async ({ page }, testInfo) => {
    if (testInfo.status !== testInfo.expectedStatus) {
      await attachEventLog(page, testInfo);
    }
  });

  test('positive and negative control: lotus (dd -> đ) vs keyboard-us (dd -> dd) vs lotus restoration', async ({
    page,
  }) => {
    const input = page.locator('#test-input');
    await ensureActive(page, input);

    // Lotus active: Telex input produces composed character
    await switchIM('lotus');
    await expect.poll(async () => await getActiveIM(), { timeout: 3000 }).toBe('lotus');
    await clearInput(page, input);
    await resetEventLog(page);
    await typeWithLotus(page, input, ['d', 'd']);
    await expect(input).toHaveValue('đ');
    // A real commit must surface as an input/compositionend event carrying the
    // composed character — something a tautological length check never proved.
    const positiveEvents = await getEventLog(page);
    expect(
      positiveEvents.some(
        (e) =>
          (e.type === 'input' || e.type === 'compositionend') &&
          e.data === 'đ' &&
          e.targetId === 'test-input'
      )
    ).toBe(true);

    // Switch to English layout: raw keys bypass input method
    await switchIM('keyboard-us');
    await expect
      .poll(async () => await getActiveIM(), { timeout: 3000 })
      .toBe('keyboard-us');
    await clearInput(page, input);
    await resetEventLog(page);
    await typeWithLotus(page, input, ['d', 'd']);
    await expect(input).toHaveValue('dd');
    const negativeEvents = await getEventLog(page);
    expect(
      negativeEvents.some(
        (e) =>
          (e.type === 'input' || e.type === 'compositionend') &&
          e.data === 'đ'
      )
    ).toBe(false);

    // Restore Lotus: composition resumes
    await switchIM('lotus');
    await expect.poll(async () => await getActiveIM(), { timeout: 3000 }).toBe('lotus');
    await clearInput(page, input);
    await resetEventLog(page);
    await typeWithLotus(page, input, ['d', 'd']);
    await expect(input).toHaveValue('đ');
  });
});
