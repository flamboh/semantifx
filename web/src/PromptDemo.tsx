import demo from "@/demo.json";

type Effect = { effect: string };

function Chain({ chain }: { chain: Effect[] }) {
  return (
    <ol className="flex flex-wrap items-center gap-1.5 text-sm" aria-label="effect chain">
      {chain.map((effect, index) => (
        <li key={`${effect.effect}-${index}`} className="flex items-center gap-1.5">
          {index > 0 && (
            <span aria-hidden className="text-muted-foreground">
              →
            </span>
          )}
          <span className="rounded-md border bg-background px-2 py-0.5 font-medium">
            {effect.effect}
          </span>
        </li>
      ))}
    </ol>
  );
}

export function PromptDemo() {
  return (
    <section className="flex flex-col gap-4">
      <h2 className="text-sm font-medium text-secondary-foreground dark:text-secondary">
        one guitar stem, six chains
      </h2>
      <div className="flex flex-col gap-2 leading-relaxed">
        <p>each prompt below was used as input to the plugin.</p>
        <ul className="flex list-disc flex-col gap-1 pl-5 marker:text-secondary-foreground dark:marker:text-secondary">
          <li>jev picked and ordered the effects</li>
          <li>clap tuned the parameters</li>
          <li>the plugin processed the audio and volume matched it to the original</li>
        </ul>
      </div>
      <figure className="flex flex-col gap-2 rounded-lg border bg-card p-4">
        <figcaption className="text-sm text-secondary-foreground dark:text-secondary">
          original
        </figcaption>
        <audio
          controls
          preload="none"
          src={demo.dry}
          aria-label="original guitar recording"
          className="w-full"
        />
      </figure>
      <ul className="flex flex-col gap-3">
        {demo.prompts.map((item) => (
          <li key={item.prompt} className="flex flex-col gap-3 rounded-lg border bg-card p-4">
            <div className="flex flex-wrap items-baseline justify-between gap-2">
              <span className="font-medium">"{item.prompt}"</span>
              <span
                className="text-sm tabular-nums text-muted-foreground"
                title="clap distance between the audio and the prompt, lower is closer"
              >
                clap distance {item.initialLoss.toFixed(2)} → {item.finalLoss.toFixed(2)}
              </span>
            </div>
            <Chain chain={item.chain} />
            <audio
              controls
              preload="none"
              src={item.audio}
              aria-label={`guitar after the prompt ${item.prompt}`}
              className="w-full"
            />
          </li>
        ))}
      </ul>
    </section>
  );
}
