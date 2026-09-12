import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { setTimeout } from 'node:timers/promises';

const execFileAsync = promisify(execFile);

const POLL_INTERVAL_MS = 50;
const POLL_DEADLINE_MS = 3000;

/**
 * Polls `check` every ~50 ms until it returns true, throwing once the deadline
 * expires. Replaces fixed settle sleeps with deterministic state polling.
 */
async function waitForState(
  check: () => Promise<boolean>,
  description: string
): Promise<void> {
  const deadline = Date.now() + POLL_DEADLINE_MS;
  for (;;) {
    if (await check()) {
      return;
    }
    if (Date.now() >= deadline) {
      throw new Error(`Timed out waiting for ${description}`);
    }
    await setTimeout(POLL_INTERVAL_MS);
  }
}

/**
 * Runs `fcitx5-remote` with no args and parses the integer exit-state code
 * (0 = not connected, 1 = inactive, 2 = active); 0 on error.
 */
async function fcitx5State(): Promise<number> {
  try {
    const { stdout } = await execFileAsync('fcitx5-remote', []);
    const code = parseInt(stdout.trim(), 10);
    return Number.isNaN(code) ? 0 : code;
  } catch {
    return 0;
  }
}

/**
 * Returns the currently active input method name (e.g. 'lotus', 'keyboard-us').
 */
export async function getActiveIM(): Promise<string> {
  const { stdout } = await execFileAsync('fcitx5-remote', ['-n']);
  return stdout.trim();
}

/**
 * Switches the active input method to the given name (e.g. 'lotus' or 'keyboard-us').
 */
export async function switchIM(name: string): Promise<void> {
  await execFileAsync('fcitx5-remote', ['-s', name]);
  await waitForState(
    async () => (await getActiveIM()) === name,
    `active input method to become '${name}'`
  );
  if (name === 'lotus') {
    // Best-effort: -o opens the input context; not all builds require it
    // after the switch.
    await execFileAsync('fcitx5-remote', ['-o']).catch(() => {});
    await waitForState(
      async () => (await fcitx5State()) === 2,
      'fcitx5 to report active state'
    );
  }
}
/**
 * Activates the input method engine (equivalent to fcitx5-remote -o).
 */
export async function activateIM(): Promise<void> {
  await execFileAsync('fcitx5-remote', ['-o']);
  await waitForState(
    async () => (await fcitx5State()) === 2,
    'fcitx5 to report active state'
  );
}

/**
 * Inactivates the input method engine (equivalent to fcitx5-remote -c).
 */
export async function inactivateIM(): Promise<void> {
  await execFileAsync('fcitx5-remote', ['-c']);
  await waitForState(
    async () => (await fcitx5State()) === 1,
    'fcitx5 to report inactive state'
  );
}

/**
 * Checks if Fcitx5 is currently running and responsive.
 * `fcitx5-remote` returns 1 (inactive) or 2 (active) when running, or 0 / error when not.
 */
export async function isFcitxRunning(): Promise<boolean> {
  const state = await fcitx5State();
  return state === 1 || state === 2;
}
