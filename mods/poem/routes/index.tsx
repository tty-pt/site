import IndexList from "@/index/IndexList.tsx";
import type { IndexProps } from "@/index/IndexList.tsx"; 
export { handler } from "@/index/IndexList.tsx";

export default function SongIndex({ data }: IndexProps) {
  return <IndexList module="poem" body={data.body} />;
}
