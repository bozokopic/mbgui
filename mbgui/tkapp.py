from tkinter import ttk
import collections
import concurrent.futures
import threading
import tkinter as tk
import typing


class TkApp:

    def __init__(self):
        self._root = tk.Tk()
        self._style = ttk.Style(self._root)
        self._executor = concurrent.futures.ThreadPoolExecutor()
        self._call_main_queue = collections.deque()
        self._call_main_lock = threading.Lock()

        self._root.bind('<<AppCall>>', self._on_app_call)

    @property
    def root(self) -> tk.Tk:
        return self._root

    @property
    def style(self) -> ttk.Style:
        return self._style

    def call_main(self,
                  fn: typing.Callable,
                  *args,
                  **kwargs):
        with self._call_main_lock:
            self._call_main_queue.append((fn, args, kwargs))
        self._root.event_generate('<<AppCall>>')

    def call_worker(self,
                    fn: typing.Callable,
                    *args,
                    **kwargs
                    ) -> concurrent.futures.Future:
        return self._executor.submit(fn, *args, **kwargs)

    def run(self):
        try:
            self._root.mainloop()

        finally:
            self._executor.shutdown()

    def _on_app_call(self, evt):
        with self._call_main_lock:
            if not self._call_main_queue:
                return

            queue = self._call_main_queue
            self._call_main_queue = collections.deque()

        for fn, args, kwargs in queue:
            fn(*args, **kwargs)
