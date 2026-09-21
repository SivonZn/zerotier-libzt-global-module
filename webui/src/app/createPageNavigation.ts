import { createMemo, createSignal, onCleanup, onMount, type Accessor } from "solid-js";
import EmblaCarousel, { type EmblaCarouselType } from "embla-carousel";

export const PAGE_IDS = ["overview", "network", "routing", "settings"] as const;
export type PageId = typeof PAGE_IDS[number];

export type PageNavigation = {
  activePage: Accessor<PageId>;
  activeIndex: Accessor<number>;
  pageProgress: Accessor<number>;
  pageDragging: Accessor<boolean>;
  setViewport: (element: HTMLDivElement) => void;
  activatePage: (page: PageId) => void;
  handleInputTouchStart: (event: TouchEvent) => void;
  handleInputTouchEnd: (event: TouchEvent) => void;
  handleInputTouchCancel: () => void;
};

export function createPageNavigation(initialPage: PageId = "overview"): PageNavigation {
  const [activePage, setActivePage] = createSignal<PageId>(initialPage);
  const [pageProgress, setPageProgress] = createSignal(PAGE_IDS.indexOf(initialPage));
  const [pageDragging, setPageDragging] = createSignal(false);
  const activeIndex = createMemo(() => PAGE_IDS.indexOf(activePage()));
  let viewport: HTMLDivElement | undefined;
  let carousel: EmblaCarouselType | undefined;
  let mounted = false;
  let inputSwipeStart: { x: number; y: number } | undefined;

  function syncCarousel() {
    if (!carousel) return;
    const progress = Math.max(0, Math.min(1, carousel.scrollProgress()));
    setPageProgress(progress * (PAGE_IDS.length - 1));
    setActivePage(PAGE_IDS[carousel.selectedScrollSnap()] ?? "overview");
  }
  function destroyCarousel() { carousel?.destroy(); carousel = undefined; setPageDragging(false); }
  function initializeCarousel() {
    if (!mounted || !viewport || carousel) return;
    carousel = EmblaCarousel(viewport, {
      align: "start",
      containScroll: "trimSnaps",
      duration: 18,
      loop: false,
      skipSnaps: false,
      watchDrag: (_api, event) => !(event.target instanceof Element && (
        event.target.closest("[data-no-page-drag]") || event.target.closest(".page-swipe-input")
      ))
    });
    carousel.on("scroll", syncCarousel);
    carousel.on("select", syncCarousel);
    carousel.on("pointerDown", () => setPageDragging(true));
    carousel.on("pointerUp", () => setPageDragging(false));
    carousel.on("settle", () => { setPageDragging(false); syncCarousel(); });
    const index = PAGE_IDS.indexOf(activePage());
    if (index > 0) carousel.scrollTo(index, true);
    syncCarousel();
  }
  function setViewport(element: HTMLDivElement) { if (viewport === element) return; destroyCarousel(); viewport = element; initializeCarousel(); }
  function activatePage(page: PageId) {
    const index = PAGE_IDS.indexOf(page);
    if (!carousel) { setActivePage(page); setPageProgress(index); return; }
    carousel.scrollTo(index);
  }
  function handleInputTouchStart(event: TouchEvent) {
    const target = event.target;
    if (!(target instanceof Element) || !target.closest(".page-swipe-input")) return;
    const touch = event.touches[0]; if (touch) inputSwipeStart = { x: touch.clientX, y: touch.clientY };
  }
  function handleInputTouchEnd(event: TouchEvent) {
    if (!inputSwipeStart) return;
    const touch = event.changedTouches[0]; const start = inputSwipeStart; inputSwipeStart = undefined;
    if (!touch) return;
    const dx = touch.clientX - start.x; const dy = touch.clientY - start.y;
    if (Math.abs(dx) < 42 || Math.abs(dx) < Math.abs(dy) * 1.2) return;
    event.preventDefault(); if (dx < 0) carousel?.scrollNext(); else carousel?.scrollPrev();
  }
  function handleInputTouchCancel() { inputSwipeStart = undefined; }
  onMount(() => { mounted = true; initializeCarousel(); });
  onCleanup(() => { mounted = false; inputSwipeStart = undefined; destroyCarousel(); viewport = undefined; });
  return { activePage, activeIndex, pageProgress, pageDragging, setViewport, activatePage, handleInputTouchStart, handleInputTouchEnd, handleInputTouchCancel };
}
