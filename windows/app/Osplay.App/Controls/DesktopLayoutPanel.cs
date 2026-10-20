using System.Windows;
using System.Windows.Controls;

namespace Osplay.App.Controls;

/// <summary>
/// Places each child at its desktop rectangle (attached Left/Top/Width/Height in pixels), scaled
/// to fit the panel with the whole arrangement centred: a picture of the PC's monitors.
/// </summary>
public sealed class DesktopLayoutPanel : Panel
{
    public static readonly DependencyProperty GapProperty = DependencyProperty.Register(
        nameof(Gap), typeof(double), typeof(DesktopLayoutPanel),
        new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsArrange));

    public static readonly DependencyProperty DesktopLeftProperty = Attached(nameof(DesktopLeftProperty));
    public static readonly DependencyProperty DesktopTopProperty = Attached(nameof(DesktopTopProperty));
    public static readonly DependencyProperty DesktopWidthProperty = Attached(nameof(DesktopWidthProperty));
    public static readonly DependencyProperty DesktopHeightProperty = Attached(nameof(DesktopHeightProperty));

    /// <summary>Space between neighbouring monitors.</summary>
    public double Gap
    {
        get => (double)GetValue(GapProperty);
        set => SetValue(GapProperty, value);
    }

    public static double GetDesktopLeft(UIElement element) => (double)element.GetValue(DesktopLeftProperty);
    public static void SetDesktopLeft(UIElement element, double value) => element.SetValue(DesktopLeftProperty, value);
    public static double GetDesktopTop(UIElement element) => (double)element.GetValue(DesktopTopProperty);
    public static void SetDesktopTop(UIElement element, double value) => element.SetValue(DesktopTopProperty, value);
    public static double GetDesktopWidth(UIElement element) => (double)element.GetValue(DesktopWidthProperty);
    public static void SetDesktopWidth(UIElement element, double value) => element.SetValue(DesktopWidthProperty, value);
    public static double GetDesktopHeight(UIElement element) => (double)element.GetValue(DesktopHeightProperty);
    public static void SetDesktopHeight(UIElement element, double value) => element.SetValue(DesktopHeightProperty, value);

    protected override Size MeasureOverride(Size availableSize)
    {
        foreach (UIElement child in InternalChildren)
        {
            child.Measure(availableSize);
        }
        return new Size(
            double.IsInfinity(availableSize.Width) ? 0 : availableSize.Width,
            double.IsInfinity(availableSize.Height) ? 0 : availableSize.Height);
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        if (InternalChildren.Count == 0)
        {
            return finalSize;
        }
        double left = double.MaxValue, top = double.MaxValue, right = double.MinValue, bottom = double.MinValue;
        foreach (UIElement child in InternalChildren)
        {
            left = Math.Min(left, GetDesktopLeft(child));
            top = Math.Min(top, GetDesktopTop(child));
            right = Math.Max(right, GetDesktopLeft(child) + GetDesktopWidth(child));
            bottom = Math.Max(bottom, GetDesktopTop(child) + GetDesktopHeight(child));
        }
        double scale = Math.Min(finalSize.Width / Math.Max(1, right - left), finalSize.Height / Math.Max(1, bottom - top));
        double offsetX = (finalSize.Width - ((right - left) * scale)) / 2;
        double offsetY = (finalSize.Height - ((bottom - top) * scale)) / 2;
        foreach (UIElement child in InternalChildren)
        {
            child.Arrange(new Rect(
                offsetX + ((GetDesktopLeft(child) - left) * scale) + (Gap / 2),
                offsetY + ((GetDesktopTop(child) - top) * scale) + (Gap / 2),
                Math.Max(0, (GetDesktopWidth(child) * scale) - Gap),
                Math.Max(0, (GetDesktopHeight(child) * scale) - Gap)));
        }
        return finalSize;
    }

    private static DependencyProperty Attached(string fieldName) => DependencyProperty.RegisterAttached(
        fieldName.Replace("Property", string.Empty, StringComparison.Ordinal), typeof(double),
        typeof(DesktopLayoutPanel),
        new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.AffectsParentArrange));
}
