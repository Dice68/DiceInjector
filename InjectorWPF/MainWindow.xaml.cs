using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;

namespace InjectorWPF;

public partial class MainWindow : Window
{
    private enum StatusKind { Ok, Warn, Error, Busy }

    private readonly ObservableCollection<ProcessItem> _view = new();
    private readonly DispatcherTimer _cursorTimer;
    private List<ProcessItem> _all = new();
    private bool _busy;

    public MainWindow()
    {
        InitializeComponent();
        ProcessList.ItemsSource = _view;
        SearchBox.Focus();

        _cursorTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(530) };
        _cursorTimer.Tick += (_, _) => ToggleCursor();
        _cursorTimer.Start();

        _ = RefreshProcessesAsync();
    }

    private void ToggleCursor()
    {
        TitleCursor.Visibility = TitleCursor.Visibility == Visibility.Visible
            ? Visibility.Hidden
            : Visibility.Visible;
        if (_busy)
            StatusCursor.Visibility = StatusCursor.Visibility == Visibility.Visible
                ? Visibility.Hidden
                : Visibility.Visible;
    }


    private void TitleBar_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ButtonState == MouseButtonState.Pressed)
            DragMove();
    }

    private void MinBtn_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void CloseBtn_Click(object sender, RoutedEventArgs e) => Close();


    private async Task RefreshProcessesAsync()
    {
        var list = await Task.Run(() =>
        {
            var result = new List<ProcessItem>();
            foreach (var p in Process.GetProcesses())
            {
                try
                {
                    result.Add(new ProcessItem(p.Id, p.ProcessName));
                }
                catch
                {

                }
            }
            return result.OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ToList();
        });
        _all = list;
        ApplyFilter();
    }

    private void ApplyFilter()
    {
        var filter = SearchBox.Text.Trim();
        var filtered = string.IsNullOrEmpty(filter)
            ? _all
            : _all.Where(p => p.Name.Contains(filter, StringComparison.OrdinalIgnoreCase)).ToList();

        _view.Clear();
        foreach (var item in filtered)
            _view.Add(item);

        ProcessCount.Text = $"{filtered.Count} / {_all.Count}";
    }

    private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        SearchPlaceholder.Visibility = string.IsNullOrEmpty(SearchBox.Text)
            ? Visibility.Visible : Visibility.Collapsed;
        ApplyFilter();
    }

    private void RefreshBtn_Click(object sender, RoutedEventArgs e) => _ = RefreshProcessesAsync();

    private void DllBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        DllPlaceholder.Visibility = string.IsNullOrEmpty(DllBox.Text)
            ? Visibility.Visible : Visibility.Collapsed;
    }


    private void BrowseBtn_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "Select DLL",
            Filter = "DLL Files (*.dll)|*.dll|All Files (*.*)|*.*",
            CheckFileExists = true,
        };
        if (dialog.ShowDialog(this) == true)
        {
            DllBox.Text = dialog.FileName;
            SetStatus("dll selected", StatusKind.Ok);
        }
    }

    private void DllBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
            _ = InjectAsync();
    }

    private void ProcessList_MouseDoubleClick(object sender, MouseButtonEventArgs e) => _ = InjectAsync();

    private async void InjectBtn_Click(object sender, RoutedEventArgs e) => await InjectAsync();

    private async Task InjectAsync()
    {
        if (_busy)
            return;

        var selected = ProcessList.SelectedItem as ProcessItem;
        if (selected is null)
        {
            SetStatus("error: select a process from the list", StatusKind.Warn);
            return;
        }

        var dll = DllBox.Text.Trim();
        if (dll.Length == 0)
        {
            SetStatus("error: select a dll file", StatusKind.Warn);
            return;
        }
        if (!File.Exists(dll))
        {
            SetStatus("error: dll file not found", StatusKind.Error);
            return;
        }

        _busy = true;
        InjectBtn.IsEnabled = false;
        InjectBtn.Content = "INJECTING...";
        StatusCursor.Visibility = Visibility.Visible;
        SetStatus($"injecting into pid {selected.Pid} ...", StatusKind.Busy);

        var result = await Task.Run(() => Injection.Inject(selected.Pid, dll));

        _busy = false;
        InjectBtn.IsEnabled = true;
        InjectBtn.Content = "INJECT";
        StatusCursor.Visibility = Visibility.Hidden;

        if (result.Ok)
            SetStatus($"injected into pid {selected.Pid}", StatusKind.Ok);
        else
            SetStatus($"error: {result.Error}", StatusKind.Error);
    }


    private void SetStatus(string message, StatusKind kind)
    {
        StatusText.Text = " " + message;
        switch (kind)
        {
            case StatusKind.Ok:
                StatusTag.Text = "[ok]";
                StatusTag.Foreground = (Brush)FindResource("Accent");
                break;
            case StatusKind.Warn:
                StatusTag.Text = "[!]";
                StatusTag.Foreground = (Brush)FindResource("Warn");
                break;
            case StatusKind.Error:
                StatusTag.Text = "[err]";
                StatusTag.Foreground = (Brush)FindResource("Error");
                break;
            case StatusKind.Busy:
                StatusTag.Text = "[..]";
                StatusTag.Foreground = (Brush)FindResource("Accent");
                break;
        }
    }
}
