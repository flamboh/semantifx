import { Button } from "@/components/ui/button";
import { ThemeToggle } from "@/theme/ThemeToggle";

const references = [
  {
    label: "text2fx",
    detail: "chu et al., icassp 2025",
    href: "https://arxiv.org/abs/2409.18847",
  },
  {
    label: "clap",
    detail: "elizalde et al., icassp 2024",
    href: "https://arxiv.org/abs/2309.05767",
  },
  {
    label: "ddsp",
    detail: "engel et al., iclr 2020",
    href: "https://arxiv.org/abs/2001.04643",
  },
  {
    label: "juce",
    detail: "plugin framework",
    href: "https://juce.com",
  },
];

const links = [
  { label: "source", href: "https://github.com/flamboh/semantifx" },
  { label: "summer talk", href: "https://present.oli.boo/semantifx-summer/" },
];

export default function App() {
  return (
    <main className="mx-auto flex w-full max-w-2xl flex-col gap-16 px-6 py-20 md:py-28">
      <header className="flex items-start justify-between gap-4">
        <div className="select-none">
          <h1 className="text-7xl font-black leading-none tracking-[-0.04em]">
            semanti<span className="text-brand">fx</span>
          </h1>
          <p className="mt-3 text-base text-muted-foreground">tune audio effects with words</p>
        </div>
        <ThemeToggle />
      </header>

      <section className="flex flex-col gap-4 text-lg leading-relaxed">
        <p>
          semantifx is an audio plugin. it listens to ten seconds of your sound, reads a prompt, and
          sets the parameters of your effect chain so the audio moves toward the prompt.
        </p>
        <p>
          the result is an ordinary effect configuration. every knob stays editable and automatable.
        </p>
        <div className="flex flex-wrap gap-2 pt-2">
          {links.map((link) => (
            <Button key={link.href} asChild variant="secondary">
              <a href={link.href} target="_blank" rel="noreferrer">
                {link.label}
              </a>
            </Button>
          ))}
        </div>
      </section>

      <section className="flex flex-col gap-4">
        <h2 className="text-sm font-medium text-secondary-foreground dark:text-secondary">
          before and after
        </h2>
        <figure className="grid gap-4 rounded-lg border bg-card p-4 md:grid-cols-2">
          <div className="flex flex-col gap-2">
            <span className="text-sm text-secondary-foreground dark:text-secondary">guitar</span>
            <audio
              controls
              preload="none"
              src="/original-guitar.wav"
              aria-label="original guitar recording"
              className="w-full"
            />
          </div>
          <div className="flex flex-col gap-2">
            <span className="text-sm text-muted-foreground">"coming through an old telephone"</span>
            <audio
              controls
              preload="none"
              src="/old-telephone-guitar.wav"
              aria-label="guitar after the prompt coming through an old telephone"
              className="w-full"
            />
          </div>
        </figure>
        <img
          src="/semantifx-ui.png"
          alt="the semantifx plugin: an eq curve, a prompt box reading coming through an old telephone, and knobs for each parameter"
          className="w-full rounded-lg border"
          width={1560}
          height={1080}
          loading="lazy"
        />
      </section>

      <section className="flex flex-col gap-4">
        <h2 className="text-sm font-medium text-secondary-foreground dark:text-secondary">
          how it works
        </h2>
        <ol className="flex list-decimal flex-col gap-3 pl-5 leading-relaxed marker:text-secondary-foreground dark:marker:text-secondary">
          <li>the plugin runs in your daw and does its own real-time audio processing.</li>
          <li>
            a local worker takes the excerpt, the prompt, and the chain. clap scores how close the
            processed audio is to the prompt, and gradient descent moves the parameters to close the
            gap.
          </li>
          <li>the worker returns a suggestion. the plugin applies it and you keep editing.</li>
        </ol>
        <p className="text-sm text-muted-foreground">
          the current build has a six-band eq and a distortion, 22 parameters in all, as an au,
          vst3, and standalone app on macos.
        </p>
      </section>

      <section className="flex flex-col gap-4">
        <h2 className="text-sm font-medium text-secondary-foreground dark:text-secondary">
          references
        </h2>
        <ul className="flex flex-col divide-y rounded-lg border bg-card">
          {references.map((reference) => (
            <li key={reference.href}>
              <a
                href={reference.href}
                target="_blank"
                rel="noreferrer"
                className="flex items-baseline justify-between gap-4 px-4 py-3 transition-colors hover:bg-accent"
              >
                <span className="font-medium">{reference.label}</span>
                <span className="text-sm text-muted-foreground">{reference.detail}</span>
              </a>
            </li>
          ))}
        </ul>
      </section>

      <footer className="text-sm text-muted-foreground">
        <a
          href="https://github.com/flamboh"
          target="_blank"
          rel="noreferrer"
          className="underline-offset-4 hover:text-foreground hover:underline"
        >
          oliver boorstein
        </a>
      </footer>
    </main>
  );
}
