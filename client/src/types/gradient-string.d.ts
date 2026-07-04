declare module "gradient-string" {
  type GradientFn = (text: string) => string;
  interface GradientString {
    passion: GradientFn;
    vice: GradientFn;
    atlas: GradientFn;
    morning: GradientFn;
    fruit: GradientFn;
    cristal: GradientFn;
    (colors: string[]): GradientFn;
  }
  const gradient: GradientString;
  export default gradient;
}
