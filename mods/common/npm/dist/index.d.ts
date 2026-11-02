export interface PageLayoutOptions {
  title?: string;
  activeNav?: string;
  theme?: string;
}

export interface PageLayout {
  chromeRoot: string;
  budRoot: string;
  meta: PageLayoutOptions;
  renderHtml(): string;
}

export declare function createPageLayout(opts?: PageLayoutOptions): PageLayout;

export interface MediaControls {
  url: string;
  duration: number;
  isPlaying: boolean;
  currentTime: number;
  play(): void;
  pause(): void;
  seek(time: number): void;
}

export declare function createMediaControls(opts?: { mediaUrl?: string; duration?: number }): MediaControls;

export interface FormActionsOptions {
  cancelHref?: string;
  submitLabel?: string;
  extraHtml?: string;
}

export interface FormActions {
  cancelHref: string;
  submitLabel: string;
  extraHtml: string;
  renderHtml(): string;
}

export declare function createFormActions(opts?: FormActionsOptions): FormActions;

export interface DeleteConfirmOptions {
  module: string;
  id: string;
  title: string;
  csrfToken?: string;
}

export interface DeleteConfirmForm {
  actionPath: string;
  cancelPath: string;
  title: string;
  csrfToken: string;
  renderHtml(): string;
}

export declare function createDeleteConfirmForm(opts: DeleteConfirmOptions): DeleteConfirmForm;

export declare function version(): string;
