from pathlib import Path
from tkinter import ttk
import sys
import tkinter as tk
import tkinter.font

from mbgui import mblaze
from mbgui.tkapp import TkApp


conf = {'fonts': {'regular': {'family': 'Inter',
                              'size': 12,
                              'weight': 'normal'},
                  'monospace': {'family': 'Roboto Mono',
                                'size': 12,
                                'weight': 'normal'}}}


def main():
    app = App(conf)
    app.run(sys.argv[1:])


class App:

    def __init__(self, conf):
        self._directories = set()
        self._selected_directory = None
        self._selected_message = None

        self._app = TkApp()

        self._fonts = {name: tk.font.Font(name=f'{name}Font', **values)
                       for name, values in conf['fonts'].items()}

        self._icons = {
            path.stem: tk.PhotoImage(file=str(path))
            for path in (Path(__file__).parent / 'icons').glob('*.png')}

        self._app.root.columnconfigure(0, weight=1)
        self._app.root.rowconfigure(0, weight=1)

        self._app.style.configure('Treeview', font=self._fonts['regular'])

        frame = ttk.Frame(self._app.root)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)
        frame.grid(row=0, column=0, sticky='nswe')

        horizontal_pane = ttk.Panedwindow(frame, orient="horizontal")
        horizontal_pane.grid(row=0, column=0, sticky='nswe')

        directories_frame = ttk.Frame(horizontal_pane)
        directories_frame.columnconfigure(0, weight=1)
        directories_frame.rowconfigure(0, weight=1)
        horizontal_pane.add(directories_frame, weight=1)

        self._directories_tree = ttk.Treeview(directories_frame,
                                              selectmode='browse',
                                              columns=('unseen', 'total'))
        self._directories_tree.column('unseen', width=32, anchor='e')
        self._directories_tree.column('total', width=32, anchor='e')
        self._directories_tree.bind('<<TreeviewSelect>>',
                                    self._on_directories_tree_select)
        self._directories_tree.grid(row=0, column=0, sticky='nswe')

        directories_scroll = ttk.Scrollbar(
            directories_frame, orient='vertical',
            command=self._directories_tree.yview)
        self._directories_tree.configure(yscrollcommand=directories_scroll.set)
        directories_scroll.grid(row=0, column=1, sticky='nswe')

        vertical_pane = ttk.Panedwindow(horizontal_pane, orient="vertical")
        horizontal_pane.add(vertical_pane, weight=2)

        messages_frame = ttk.Frame(vertical_pane)
        messages_frame.columnconfigure(0, weight=1)
        messages_frame.rowconfigure(0, weight=1)
        vertical_pane.add(messages_frame, weight=1)

        self._messages_tree = ttk.Treeview(messages_frame,
                                           selectmode='browse',
                                           columns=('sender', 'date'))
        self._messages_tree.column('sender', width=32, anchor='w')
        self._messages_tree.column('date', width=32, anchor='w')
        self._messages_tree.bind('<<TreeviewSelect>>',
                                 self._on_messages_tree_select)
        self._messages_tree.bind('<Double-Button-1>',
                                 self._on_messages_tree_double)
        self._messages_tree.grid(row=0, column=0, sticky='nswe')

        messages_scroll = ttk.Scrollbar(
            messages_frame, orient='vertical',
            command=self._messages_tree.yview)
        self._messages_tree.configure(yscrollcommand=messages_scroll.set)
        messages_scroll.grid(row=0, column=1, sticky='nswe')

        message_frame = ttk.Frame(vertical_pane)
        message_frame.columnconfigure(0, weight=1)
        message_frame.rowconfigure(0, weight=1)
        vertical_pane.add(message_frame, weight=1)

        self._message_text = tk.Text(message_frame,
                                     font=self._fonts['monospace'])
        self._message_text.grid(row=0, column=0, sticky='nswe')

        message_scroll_vertical = ttk.Scrollbar(
            message_frame, orient='vertical',
            command=self._message_text.yview)
        self._message_text.configure(
            yscrollcommand=message_scroll_vertical.set)
        message_scroll_vertical.grid(row=0, column=1, sticky='nswe')

    def run(self, args):
        self._get_directories(args)
        self._app.run()

    def _get_directories(self, paths):
        def on_done(f):
            self._app.call_main(self._on_directories, f.result())

        f = self._app.call_worker(mblaze.get_directories, paths)
        f.add_done_callback(on_done)

    def _get_directory_unseen(self, path):
        def on_done(f):
            self._app.call_main(self._on_directory_unseen, path, f.result())

        f = self._app.call_worker(mblaze.get_directory_unseen, path)
        f.add_done_callback(on_done)

    def _get_directory_total(self, path):
        def on_done(f):
            self._app.call_main(self._on_directory_total, path, f.result())

        f = self._app.call_worker(mblaze.get_directory_total, path)
        f.add_done_callback(on_done)

    def _get_messages(self, path):
        def on_done(f):
            self._app.call_main(self._on_messages, path, f.result())

        f = self._app.call_worker(mblaze.get_messages, path)
        f.add_done_callback(on_done)

    def _get_message(self, path):
        def on_done(f):
            self._app.call_main(self._on_message, path, f.result())

        f = self._app.call_worker(mblaze.get_message, path)
        f.add_done_callback(on_done)

    def _on_directories(self, directories):
        self._directories = set()
        directory_ids = self._directories_tree.get_children()
        if directory_ids:
            self._directories_tree.delete(*directory_ids)

        def add_directory(parent_id, directory):
            directory_id = (str(Path(parent_id) / directory.name) if parent_id
                            else directory.name)
            if directory.is_leaf:
                self._directories.add(directory_id)
            icon = self._icons['inbox-16' if directory.is_leaf
                               else 'folder-16']
            self._directories_tree.insert(parent_id, 'end',
                                          id=directory_id,
                                          text=f' {directory.name}',
                                          image=icon,
                                          values=('', ''),
                                          open=True)
            if directory.is_leaf:
                self._get_directory_unseen(directory_id)
                self._get_directory_total(directory_id)
            for child in directory.children:
                add_directory(directory_id, child)

        for directory in directories:
            add_directory('', directory)

    def _on_directory_unseen(self, directory, count):
        values = self._directories_tree.item(directory, 'values')
        values = str(count), values[1]
        self._directories_tree.item(directory, values=values)

    def _on_directory_total(self, directory, count):
        values = self._directories_tree.item(directory, 'values')
        values = values[0], str(count)
        self._directories_tree.item(directory, values=values)

    def _on_directories_tree_select(self, evt):
        selection = self._directories_tree.selection()
        directory = selection[0] if selection else None
        directory = directory if directory in self._directories else None
        if directory == self._selected_directory:
            return

        self._selected_directory = directory

        message_ids = self._messages_tree.get_children()
        if message_ids:
            self._messages_tree.delete(*message_ids)

        if not directory:
            return

        self._get_messages(directory)

    def _on_messages(self, directory, messages):
        if directory != self._selected_directory:
            return

        def add_message(parent_id, message):
            icon = _status_icon(message.status)
            icon = self._icons[f'{icon}-16'] if icon else ''
            self._messages_tree.insert(parent_id, 'end',
                                       id=message.path,
                                       text=f' {message.subject}',
                                       image=icon,
                                       values=(message.sender, message.date))
            for child in message.children:
                add_message(message.path, child)

        for message in messages:
            add_message('', message)

    def _on_messages_tree_select(self, evt):
        selection = self._messages_tree.selection()
        message = selection[0] if selection else None
        if message == self._selected_message:
            return

        self._selected_message = message
        self._message_text.delete('1.0', 'end')

        if not message:
            return

        self._get_message(message)

    def _on_messages_tree_double(self, evt):
        if self._selected_message:
            print(self._selected_message)

    def _on_message(self, message, text):
        if message != self._selected_message:
            return

        self._message_text.insert('1.0', text)


def _status_icon(status):
    if status == mblaze.Status.SEEN:
        return 'file'

    if status == mblaze.Status.FLAGGED:
        return 'flag'

    if status == mblaze.Status.UNSEEN:
        return 'mail'

    if status == mblaze.Status.TRASHED:
        return 'trash'

    if status == mblaze.Status.VIRTUAL:
        return 'eye-off'

    raise ValueError('unsupported status')


if __name__ == '__main__':
    main()
