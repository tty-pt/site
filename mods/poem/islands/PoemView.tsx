interface PoemViewProps {
  id: string;
  lang: string;
}

export default function PoemView({ id, lang }: PoemViewProps) {
  return (
    <iframe
      src={`/poem/${id}/${lang}.html`}
      class="w-full"
      style="height: 80vh; border: none;"
    />
  );
}

