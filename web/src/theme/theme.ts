export const THEME_STORAGE_KEY = "semantifx:theme";

export type Theme = "light" | "dark";

const isTheme = (value: string | null): value is Theme => value === "light" || value === "dark";

export const resolveInitialTheme = (): Theme => {
  try {
    const storedTheme = window.localStorage.getItem(THEME_STORAGE_KEY);
    if (isTheme(storedTheme)) return storedTheme;
  } catch {
    // Storage may be unavailable in private browsing mode.
  }

  return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
};

export const applyTheme = (mode: Theme) => {
  const root = document.documentElement;
  root.classList.toggle("dark", mode === "dark");
  root.dataset.theme = mode;
};

export const setStoredTheme = (mode: Theme) => {
  try {
    window.localStorage.setItem(THEME_STORAGE_KEY, mode);
  } catch {
    // Storage may be unavailable in private browsing mode.
  }
};
