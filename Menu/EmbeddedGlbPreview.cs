using System.Collections;
using System.IO;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace RonMenu;

internal sealed class EmbeddedGlbPreview : UserControl
{
    private static readonly object SharedModelLock = new();
    private static Task<Model3DGroup>? _sharedModelTask;
    private readonly ModelVisual3D _modelHost = new();
    private readonly Viewport3D _viewport;
    private readonly Image _cachedImage;
    private bool _loading;

    public EmbeddedGlbPreview()
    {
        var scene = new Model3DGroup();
        scene.Children.Add(new AmbientLight(Color.FromRgb(112, 143, 155)));
        scene.Children.Add(new DirectionalLight(Color.FromRgb(225, 241, 247), new Vector3D(-0.4, -0.8, -1)));
        _modelHost.Content = scene;

        _viewport = new Viewport3D
        {
            ClipToBounds = true,
            Camera = new PerspectiveCamera(new Point3D(0, 1.25, 4.8), new Vector3D(0, 0, -1), new Vector3D(0, 1, 0), 32),
            Children = { _modelHost }
        };
        _cachedImage = new Image { Stretch = System.Windows.Media.Stretch.Uniform, Visibility = Visibility.Collapsed, IsHitTestVisible = false };
        var layers = new Grid();
        layers.Children.Add(_viewport);
        layers.Children.Add(_cachedImage);
        Content = layers;
        Loaded += async (_, _) => await LoadModelAsync();
    }

    private async Task LoadModelAsync()
    {
        if (_loading || _cachedImage.Source is not null) return;
        _loading = true;
        try
        {
            Task<Model3DGroup> shared;
            lock (SharedModelLock)
                shared = _sharedModelTask ??= Task.Run(LoadEmbeddedModel);
            var model = await shared;
            _modelHost.Content = model;
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.Render);
            var width = Math.Max(220, (int)ActualWidth * 2);
            var height = Math.Max(330, (int)ActualHeight * 2);
            var bitmap = new RenderTargetBitmap(width, height, 192, 192, PixelFormats.Pbgra32);
            bitmap.Render(_viewport);
            bitmap.Freeze();
            _cachedImage.Source = bitmap;
            _viewport.Visibility = Visibility.Collapsed;
            _cachedImage.Visibility = Visibility.Visible;
        }
        catch
        {
            // The surrounding aim panel remains usable if an embedded asset is corrupt.
        }
        finally { _loading = false; }
    }

    private static Model3DGroup LoadEmbeddedModel()
    {
        using var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("RonMenu.Model.glb")
            ?? throw new InvalidDataException("Embedded aim model was not found.");
        using var reader = new BinaryReader(stream);
        if (reader.ReadUInt32() != 0x46546C67 || reader.ReadUInt32() != 2) throw new InvalidDataException("Unsupported GLB.");
        _ = reader.ReadUInt32();
        var jsonLength = reader.ReadUInt32();
        _ = reader.ReadUInt32();
        var json = new JavaScriptSerializer().DeserializeObject(System.Text.Encoding.UTF8.GetString(reader.ReadBytes(checked((int)jsonLength)))) as Dictionary<string, object>
            ?? throw new InvalidDataException("Invalid GLB document.");
        var binLength = reader.ReadUInt32();
        _ = reader.ReadUInt32();
        var binary = reader.ReadBytes(checked((int)binLength));

        var views = Array(json, "bufferViews");
        var accessors = Array(json, "accessors");
        var meshes = Array(json, "meshes");
        var mesh = Dict(meshes[0]);
        var primitive = Dict(Array(mesh, "primitives")[0]);
        var attributes = Dict(primitive["attributes"]);
        var positionAccessor = Dict(accessors[Convert.ToInt32(attributes["POSITION"])]);
        var indexAccessor = Dict(accessors[Convert.ToInt32(primitive["indices"])]);
        var positionView = Dict(views[Convert.ToInt32(positionAccessor["bufferView"])]);
        var indexView = Dict(views[Convert.ToInt32(indexAccessor["bufferView"])]);
        var textureAccessor = Dict(accessors[Convert.ToInt32(attributes["TEXCOORD_0"])]);
        var textureView = Dict(views[Convert.ToInt32(textureAccessor["bufferView"])]);
        int vertexCount = Convert.ToInt32(positionAccessor["count"]);
        int indexCount = Convert.ToInt32(indexAccessor["count"]);
        int positionOffset = Convert.ToInt32(positionView["byteOffset"]) + OptionalInt(positionAccessor, "byteOffset");
        int indexOffset = Convert.ToInt32(indexView["byteOffset"]) + OptionalInt(indexAccessor, "byteOffset");
        int textureOffset = Convert.ToInt32(textureView["byteOffset"]) + OptionalInt(textureAccessor, "byteOffset");

        var node = Dict(Array(json, "nodes")[0]);
        var rotation = node.TryGetValue("rotation", out var rawRotation) ? (object[])rawRotation : new object[] { 0d, 0d, 0d, 1d };
        double qx = Convert.ToDouble(rotation[0]), qy = Convert.ToDouble(rotation[1]), qz = Convert.ToDouble(rotation[2]), qw = Convert.ToDouble(rotation[3]);

        double minX = double.MaxValue, minY = double.MaxValue, minZ = double.MaxValue;
        double maxX = double.MinValue, maxY = double.MinValue, maxZ = double.MinValue;
        for (int i = 0, offset = positionOffset; i < vertexCount; i++, offset += 12)
        {
            double x = BitConverter.ToSingle(binary, offset);
            double y = BitConverter.ToSingle(binary, offset + 4);
            double z = BitConverter.ToSingle(binary, offset + 8);
            double tx = (1 - 2 * (qy * qy + qz * qz)) * x + 2 * (qx * qy - qz * qw) * y + 2 * (qx * qz + qy * qw) * z;
            double ty = 2 * (qx * qy + qz * qw) * x + (1 - 2 * (qx * qx + qz * qz)) * y + 2 * (qy * qz - qx * qw) * z;
            double tz = 2 * (qx * qz - qy * qw) * x + 2 * (qy * qz + qx * qw) * y + (1 - 2 * (qx * qx + qy * qy)) * z;
            minX = Math.Min(minX, tx); maxX = Math.Max(maxX, tx);
            minY = Math.Min(minY, ty); maxY = Math.Max(maxY, ty);
            minZ = Math.Min(minZ, tz); maxZ = Math.Max(maxZ, tz);
        }

        double centerX = (minX + maxX) * 0.5, centerY = (minY + maxY) * 0.5, centerZ = (minZ + maxZ) * 0.5;
        double scale = 2.45 / Math.Max(0.001, maxY - minY);
        var positions = new Point3DCollection(vertexCount);
        var textureCoordinates = new PointCollection(vertexCount);
        for (int i = 0, offset = positionOffset; i < vertexCount; i++, offset += 12)
        {
            double x = BitConverter.ToSingle(binary, offset);
            double y = BitConverter.ToSingle(binary, offset + 4);
            double z = BitConverter.ToSingle(binary, offset + 8);
            double tx = (1 - 2 * (qy * qy + qz * qz)) * x + 2 * (qx * qy - qz * qw) * y + 2 * (qx * qz + qy * qw) * z;
            double ty = 2 * (qx * qy + qz * qw) * x + (1 - 2 * (qx * qx + qz * qz)) * y + 2 * (qy * qz - qx * qw) * z;
            double tz = 2 * (qx * qz - qy * qw) * x + 2 * (qy * qz + qx * qw) * y + (1 - 2 * (qx * qx + qy * qy)) * z;
            positions.Add(new Point3D((tx - centerX) * scale, (ty - minY) * scale, (tz - centerZ) * scale));
            int uv = textureOffset + i * 8;
            textureCoordinates.Add(new Point(BitConverter.ToSingle(binary, uv), BitConverter.ToSingle(binary, uv + 4)));
        }
        var triangles = new Int32Collection(indexCount);
        for (int i = 0; i < indexCount; i++)
        {
            int offset = indexOffset + i * 4;
            triangles.Add(unchecked((int)BitConverter.ToUInt32(binary, offset)));
        }
        positions.Freeze();
        textureCoordinates.Freeze();
        triangles.Freeze();

        var geometry = new MeshGeometry3D { Positions = positions, TextureCoordinates = textureCoordinates, TriangleIndices = triangles };
        geometry.Freeze();
        var material = new EmissiveMaterial(LoadBaseColorTexture(json, views, binary));
        material.Freeze();
        var model = new GeometryModel3D(geometry, material) { BackMaterial = material };
        model.Freeze();

        var group = new Model3DGroup();
        group.Children.Add(new AmbientLight(Color.FromRgb(112, 143, 155)));
        group.Children.Add(new DirectionalLight(Color.FromRgb(225, 241, 247), new Vector3D(-0.4, -0.8, -1)));
        group.Children.Add(model);
        group.Freeze();
        return group;
    }

    private static ImageBrush LoadBaseColorTexture(IDictionary<string, object> json, object[] views, byte[] binary)
    {
        var materialInfo = Dict(Array(json, "materials")[0]);
        var pbr = Dict(materialInfo["pbrMetallicRoughness"]);
        var baseColor = Dict(pbr["baseColorTexture"]);
        var texture = Dict(Array(json, "textures")[Convert.ToInt32(baseColor["index"])]);
        var image = Dict(Array(json, "images")[Convert.ToInt32(texture["source"])]);
        var view = Dict(views[Convert.ToInt32(image["bufferView"])]);
        int offset = Convert.ToInt32(view["byteOffset"]);
        int length = Convert.ToInt32(view["byteLength"]);
        using var imageStream = new MemoryStream(binary, offset, length, false);
        var bitmap = new BitmapImage();
        bitmap.BeginInit();
        bitmap.CacheOption = BitmapCacheOption.OnLoad;
        bitmap.StreamSource = imageStream;
        bitmap.EndInit();
        bitmap.Freeze();
        var brush = new ImageBrush(bitmap) { Stretch = System.Windows.Media.Stretch.Fill };
        brush.Freeze();
        return brush;
    }

    private static object[] Array(IDictionary<string, object> source, string key) => (object[])source[key];
    private static Dictionary<string, object> Dict(object source) => (Dictionary<string, object>)source;
    private static int OptionalInt(IDictionary<string, object> source, string key) => source.TryGetValue(key, out var value) ? Convert.ToInt32(value) : 0;
}
