#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gtk-3.0/gtk/gtk.h"
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include "iotools.h"
#include "stringtools.h"

#define TILE_SIZE 256 // rozmiar kafelka
#define CACHE_DIR "cache" // nazwa folderu na kafelki
#define RETRY_Q 3 // iloœc prób pobrania kafelka w przypadku niepowodzenia
//#define DEFAULT_URL "https://a.tile.opentopomap.org/{zoom}/{x}/{y}.png"
#define DEFAULT_URL "https://a.tile.opentopomap.org/{zoom}/{x}/{y}.png"

//opcje CLI
static gboolean nogui = FALSE;
static gdouble longitudeCLI = 0.0;
static gdouble latitudeCLI = 0.0;
static gint zoomCLI = 2;
static gint widthCLI = 768;
static gint heightCLI = 512;
static gchar *urlCLI = DEFAULT_URL;
static gchar *outputCLI = "map.jpg";

typedef struct { // parametry pobierania mapy
	gdouble longitude, latitude;
	gint32 zoom, width, height;
} MapRequest;

typedef struct { // ustawienia programu
	gboolean useCache;
	gboolean retry;
	gchar *url;
} Settings;

GtkWidget *window;
GtkWidget *buttonDownload;
GtkWidget *buttonSettings;
GtkWidget *buttonSave;
GtkWidget *imageMap;
GtkWidget *inputLongitude;
GtkWidget *inputLatitude;
GtkWidget *inputZoom;
GtkWidget *inputWidth;
GtkWidget *inputHeight;
GtkWidget *dialog;
Settings settings;

gint32 mod(gint32 a, gint32 b) { // modulo dzia³aj¹ce dla liczb ujemnych
	if (b < 0) {
		return mod(a, -b);
	}
	return (a % b < 0) ? (a % b + b) : (a % b);
}

gboolean downloadTile(guint32 x, guint32 y, guint32 zoom) {
	gchar cmd[1000] = { 0 };
	snprintf(cmd, 1000, "curl -s %s --output %s/tile-{zoom}-{x}-{y}.png", settings.url, CACHE_DIR);

	gchar tileX[25] = { 0 };
	snprintf(tileX, 25, "%d", x);

	gchar tileY[25] = { 0 };
	snprintf(tileY, 25, "%d", y);

	gchar tileZoom[25] = { 0 };
	snprintf(tileZoom, 25, "%d", zoom);

	//tworzymy kilka wskaŸników bo musimy zwolnic poprzednie
	gchar *cmd1 = str_replace(cmd, "{x}", tileX);
	gchar *cmd2 = str_replace(cmd1, "{y}", tileY);
	gchar *cmd3 = str_replace(cmd2, "{zoom}", tileZoom);

	printf("%s\n", cmd3);

	gint32 result = system(cmd3);
	free(cmd1);
	free(cmd2);
	free(cmd3);
	return result != -1;
}

gboolean clearCache() {
	if (!removeDirectory(CACHE_DIR)) {
		return FALSE;
	}
	if (!createDirectory(CACHE_DIR)) {
		return FALSE;
	}
	return TRUE;
}

void clearCacheRequest() {
	if (clearCache()) {
		printf("Cache cleared\n");
	} else {
		fprintf(stderr, "Failed to clear cache\n");
	}
}

void openSettings() { // otwieramy okienko z ustawieniami
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL;
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(window), flags, "_Cancel", GTK_RESPONSE_REJECT, "_OK", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	GtkWidget *grid = gtk_grid_new();

	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

	GtkWidget *buttonUseCache = gtk_toggle_button_new_with_label("Use cache");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonUseCache), settings.useCache);
	gtk_grid_attach(GTK_GRID(grid), buttonUseCache, 0, 0, 1, 1);
	gtk_widget_set_hexpand(buttonUseCache, TRUE);

	GtkWidget *buttonRetry = gtk_toggle_button_new_with_label("Retry downloading");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(buttonRetry), settings.retry);
	gtk_grid_attach(GTK_GRID(grid), buttonRetry, 0, 1, 1, 1);
	gtk_widget_set_hexpand(buttonRetry, TRUE);

	GtkWidget *buttonClearCache = gtk_button_new_with_label("Clear cache");
	gtk_grid_attach(GTK_GRID(grid), buttonClearCache, 0, 2, 1, 1);
	gtk_widget_set_hexpand(buttonClearCache, TRUE);

	GtkWidget *entryUrl = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(entryUrl), 1000);
	gtk_entry_set_text(GTK_ENTRY(entryUrl), settings.url);
	gtk_editable_set_editable(GTK_EDITABLE(entryUrl), FALSE);
	gtk_grid_attach(GTK_GRID(grid), entryUrl, 0, 3, 1, 1);

	gtk_container_add(GTK_CONTAINER(content_area), grid);
	gtk_widget_show_all(dialog);

	g_signal_connect(buttonClearCache, "clicked", G_CALLBACK(clearCacheRequest), NULL);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		settings.useCache = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonUseCache));
		settings.retry = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(buttonRetry));
		// nie mo¿na przekopiowac z innego w¹tku
//		 strncpy(settings.url, gtk_entry_get_text(GTK_ENTRY(entryUrl)), 1);
	}
	gtk_widget_destroy(dialog);
}

gpointer createMap(gpointer arg) {
	const guint32 tileSize = TILE_SIZE;
	const gdouble longitude = ((MapRequest*) arg)->longitude; // [-90, 90]
	const gdouble latitude = ((MapRequest*) arg)->latitude; // [-180, 180]
	const gint32 submapHeight = ((MapRequest*) arg)->height; // wysokoœc obrazu do pobrania w pikselach
	const gint32 submapWidth = ((MapRequest*) arg)->width; // szerokoœc obrazu do pobrania w pikselach
	const gint32 zoom = ((MapRequest*) arg)->zoom; // [0, 17]

	const gdouble normalizedLongitude = longitude / 90.0 / 2.0;
	const gdouble normalizedLatitude = latitude / 180.0 / 2.0;

	const gdouble normalizedLongitudeRad = normalizedLongitude * M_PI;
	const gdouble normalizedLatitudeRad = normalizedLatitude * M_PI * 2;

	const gint32 tiles = 1 << zoom; // iloœc kafelków na jeden bok = 2^zoom
	const gint32 mapSize = tileSize * tiles; // rozmiar jednego boku ca³ej mapy

	//https://en.wikipedia.org/wiki/Web_Mercator
	const gdouble submapNormalizedCenterX = 128 / M_PI * tiles * (normalizedLatitudeRad + M_PI) / mapSize;
	const gdouble submapNormalizedCenterY = 128 / M_PI * tiles * (M_PI - log(tan(M_PI / 4 + normalizedLongitudeRad / 2))) / mapSize; // [0, 1]

	const gdouble tileNormalizedSize = (gdouble) tileSize / mapSize;

	const gdouble submapNormalizedMaxX = submapNormalizedCenterX + (gdouble) submapWidth / mapSize / 1.0;
	const gdouble submapNormalizedMaxY = submapNormalizedCenterY + (gdouble) submapHeight / mapSize / 1.0;
	const gdouble submapNormalizedMinX = submapNormalizedCenterX - (gdouble) submapWidth / mapSize / 1.0;
	const gdouble submapNormalizedMinY = submapNormalizedCenterY - (gdouble) submapHeight / mapSize / 1.0;

	const gint32 mapMaxTileX = ceil(submapNormalizedMaxX * tiles) - 1.0;
	const gint32 mapMaxTileY = ceil(submapNormalizedMaxY * tiles) - 1.0;
	const gint32 mapMinTileX = floor(submapNormalizedMinX * tiles);
	const gint32 mapMinTileY = floor(submapNormalizedMinY * tiles);

	const gdouble mapNormalizedMaxX = (mapMaxTileX) * tileNormalizedSize;
	const gdouble mapNormalizedMaxY = (mapMaxTileY) * tileNormalizedSize;
	const gdouble mapNormalizedMinX = (mapMinTileX) * tileNormalizedSize;
	const gdouble mapNormalizedMinY = (mapMinTileY) * tileNormalizedSize;
	const gdouble mapNormalizedCenterX = (mapNormalizedMaxX + mapNormalizedMinX) / 2.0;
	const gdouble mapNormalizedCenterY = (mapNormalizedMaxY + mapNormalizedMinY) / 2.0;

	const gint32 mapColumns = mapMaxTileX - mapMinTileX + 1; // iloœc kolumn do pobrania
	const gint32 mapRows = mapMaxTileY - mapMinTileY + 1; // iloœc rzêdów do pobrania

	const gint32 mapWidth = tileSize * mapColumns; // rozmiar kolumn pobranych kafelków w pikselach
	const gint32 mapHeight = tileSize * mapRows; // rozmiar rzêdów pobranych kafelków w pikselach

	const gdouble normalizedDisplacementX = mapNormalizedCenterX - submapNormalizedCenterX;
	const gdouble normalizedDisplacementY = mapNormalizedCenterY - submapNormalizedCenterY;

	const gint32 displacementX = (gint32) (normalizedDisplacementX * mapSize);
	const gint32 displacementY = (gint32) (normalizedDisplacementY * mapSize);

	const gint32 srcX = (mapWidth - submapWidth) / 2 - displacementX;
	const gint32 srcY = (mapHeight - submapHeight) / 2 - displacementY;

#define DEBUG
#ifdef DEBUG
	printf("longitude: %lf\n", longitude);
	printf("latitude: %lf\n", latitude);
	printf("normalized longitude: %lf\n", normalizedLongitude);
	printf("normalized latitude: %lf\n", normalizedLatitude);
	printf("normalized longitude radians: %lf\n", normalizedLongitudeRad);
	printf("normalized latitude radians: %lf\n", normalizedLatitudeRad);
	printf("zoom: %u\n", zoom);
	printf("width: %u\n", submapWidth);
	printf("height: %u\n", submapHeight);
	printf("tiles: %d\n", tiles);

	printf("absolute map size: %dx%d\n", mapSize, mapSize);
	printf("map size: %dx%d\n", mapWidth, mapHeight);
	printf("submap size: %dx%d\n", submapWidth, submapHeight);
	printf("tile normalized size: %lfx%lf\n", 1.0 / tiles, 1.0 / tiles);
	printf("normalized pixel size: %lf\n", 1.0 / mapSize);

	printf("columns: %d\n", mapColumns);
	printf("rows: %d\n", mapRows);

	printf("normalized map max: <%lf, %lf>\n", mapNormalizedMaxX, mapNormalizedMaxY);
	printf("normalized map min: <%lf, %lf>\n", mapNormalizedMinX, mapNormalizedMinY);
	printf("normalized map center: <%lf %lf>\n", mapNormalizedCenterX, mapNormalizedCenterY);
	printf("map center: <%lf %lf>\n", mapNormalizedCenterX * mapSize, mapNormalizedCenterY * mapSize);

	printf("submap max: <%lf %lf>\n", submapNormalizedMaxX * mapSize, submapNormalizedMaxY * mapSize);
	printf("submap min: <%lf %lf>\n", submapNormalizedMinX * mapSize, submapNormalizedMinY * mapSize);
	printf("normalized submap max: <%lf %lf>\n", submapNormalizedMaxX, submapNormalizedMaxY);
	printf("normalized submap min: <%lf %lf>\n", submapNormalizedMinX, submapNormalizedMinY);
	printf("normalized submap center: <%lf %lf>\n", submapNormalizedCenterX, submapNormalizedCenterY);

	printf("tile max <%d, %d>\n", mapMaxTileX, mapMaxTileY);
	printf("tile min <%d, %d>\n", mapMinTileX, mapMinTileY);

	printf("normalized displacement: <%lf, %lf>\n", normalizedDisplacementX, normalizedDisplacementY);
	printf("displacement: <%d, %d>\n", displacementX, displacementY);
	printf("srcX: %d\n", srcX);
	printf("srcY: %d\n", srcY);
#endif

	GdkPixbuf* mapBuffer = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, mapWidth, mapHeight);
	GdkPixbuf* submapBuffer = gdk_pixbuf_new_subpixbuf(mapBuffer, srcX, srcY, submapWidth, submapHeight);

	if (!directoryExists(CACHE_DIR)) {
		if (!createDirectory(CACHE_DIR)) {
			printf("Nie udalo stworzyc sie folderu na kafelki.");
			gtk_widget_set_sensitive(buttonDownload, TRUE);
			gtk_widget_set_sensitive(buttonSettings, TRUE);
			return NULL;
		}
	}

	for (gint32 y = mapMinTileY; y <= mapMaxTileY; y++) {
		for (gint32 x = mapMinTileX; x <= mapMaxTileX; x++) {
			const gint32 wrappedX = mod(x, tiles);
			const gint32 wrappedY = mod(y, tiles);

			gchar path[200] = { 0 };
			sprintf(path, "%s/tile-%d-%d-%d.png", CACHE_DIR, zoom, wrappedX, wrappedY);

			gint32 q = 0; // iloœc prób

			if (!fileExists(path) || !settings.useCache) { // sprawdzamy czy plik istnieje
				download: do {
					if (q++ > RETRY_Q) { // sprawdzamy iloœc prób
						gtk_widget_set_sensitive(buttonDownload, TRUE);
						gtk_widget_set_sensitive(buttonSettings, TRUE);
						return NULL;
					}

					if (downloadTile(wrappedX, wrappedY, zoom)) { // jeœli nie to pobieramy
						break;
					} else {
						fprintf(stderr, "Error while downloading tile x:%d y:%d zoomCLI:%d\n", wrappedX, wrappedY, zoom);
						gtk_widget_set_sensitive(buttonDownload, TRUE);
						gtk_widget_set_sensitive(buttonSettings, TRUE);
					}
				} while (settings.retry);
			} else {
#ifdef DEBUG
				printf("Plik istnieje %s\n", path);
#endif
			}

			GError *error = NULL;
			GdkPixbuf *tileBuffer = gdk_pixbuf_new_from_file(path, &error); // buffer na jeden kafelek
			if (error != NULL) {
				fprintf(stderr, "Unable to read file: %s %s\n", path, error->message);
				if (settings.retry && q < RETRY_Q) { // jeœli kafelek jest uszkodzony to próbujemy pobrac go ponownie
					fprintf(stderr, "Retrying to download corrupted tile\n");
					goto download;
				}
				gtk_widget_set_sensitive(buttonDownload, TRUE);
				gtk_widget_set_sensitive(buttonSettings, TRUE);
				g_error_free(error);
				return NULL;
			}

			gdk_pixbuf_copy_area(tileBuffer, 0, 0, tileSize, tileSize, mapBuffer, (tileSize * (x - mapMinTileX)), (tileSize * (y - mapMinTileY))); // kopiujemy kafelek do mapy
			gtk_image_set_from_pixbuf(GTK_IMAGE(imageMap), submapBuffer); // odœwie¿amy mapê w interfejsie
			g_object_unref(tileBuffer);
		}
	}
	gtk_widget_set_sensitive(buttonDownload, TRUE);
	gtk_widget_set_sensitive(buttonSettings, TRUE);
	gtk_widget_set_sensitive(buttonSave, TRUE);
	return NULL;
}

void saveMapToFile(gchar *path) {
	GError *error = NULL;
	gdk_pixbuf_save(gtk_image_get_pixbuf(GTK_IMAGE(imageMap)), path, "jpeg", &error, "quality", "100", NULL);
	if (error != NULL) {
		fprintf(stderr, "Unable to read file: %s\n", error->message);
		g_error_free(error);
	}
}

void saveMapToFileDialog() {
	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
	gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		gchar *filename = gtk_file_chooser_get_filename(chooser);
		saveMapToFile(filename);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}

void validateMapRequest(MapRequest* req) {
	if (req->zoom < 0) {
		req->zoom = 0;
	} else if (req->zoom > 17) {
		req->zoom = 17;
	}
	if (req->width < 0) {
		req->width = 256;
	}
	if (req->height < 0) {
		req->height = 256;
	}
}

void downloadMap() {
	gtk_widget_set_sensitive(buttonDownload, FALSE); //blokujemy przyciski
	gtk_widget_set_sensitive(buttonSave, FALSE);
	gtk_widget_set_sensitive(buttonSettings, FALSE);

	MapRequest* coords = calloc(1, sizeof(MapRequest));
	if (nogui) { // pobieramy parametry mapy z cli
		coords->longitude = longitudeCLI;
		coords->latitude = latitudeCLI;
		coords->zoom = zoomCLI;
		coords->width = widthCLI;
		coords->height = heightCLI;
		validateMapRequest(coords);
		createMap(coords);
		saveMapToFile(outputCLI);
		exit(0);
	} else { // pobieramy parametry mapy z interfejsu
//		MapRequest* coords = calloc(1, sizeof(MapRequest));
		coords->longitude = strtod(gtk_entry_get_text(GTK_ENTRY(inputLongitude)), NULL);
		coords->latitude = strtod(gtk_entry_get_text(GTK_ENTRY(inputLatitude)), NULL);
		coords->zoom = strtod(gtk_entry_get_text(GTK_ENTRY(inputZoom)), NULL);
		coords->width = strtod(gtk_entry_get_text(GTK_ENTRY(inputWidth)), NULL);
		coords->height = strtod(gtk_entry_get_text(GTK_ENTRY(inputHeight)), NULL);
		validateMapRequest(coords);
		pthread_create(NULL, NULL, createMap, (void*) coords);
	}
}

GtkWidget* entry_new(gchar* text, gchar* placeholder, gchar* tooltip) { //funkcja pomocnicza
	GtkWidget* entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(entry), text);
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
	gtk_widget_set_tooltip_text(entry, tooltip);
	return entry;
}

void activate(GtkApplication *app) {
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget * gridTop = gtk_grid_new();

	window = gtk_application_window_new(app);
	imageMap = gtk_image_new();
	dialog = gtk_file_chooser_dialog_new("Save Map", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

	inputLongitude = entry_new("", "Longitude", "Longitude");
	inputLatitude = entry_new("", "Latitude", "Latitude");
	inputZoom = entry_new("2", "Zoom", "Zoom");
	inputWidth = entry_new("768", "Width", "Width");
	inputHeight = entry_new("512", "Height", "Height");
	buttonDownload = gtk_button_new_with_label("Download");
	buttonSave = gtk_button_new_with_label("Save");
	buttonSettings = gtk_button_new_with_label("Settings");

	gtk_window_set_title(GTK_WINDOW(window), "MapViewer");
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);
	gtk_container_add(GTK_CONTAINER(window), grid);

	gtk_container_add(GTK_CONTAINER(scroll), imageMap);
	gtk_widget_set_vexpand(scroll, TRUE);

	gtk_grid_attach(GTK_GRID(grid), gridTop, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), scroll, 0, 1, 1, 1);

	g_signal_connect(buttonDownload, "clicked", G_CALLBACK(downloadMap), NULL);
	g_signal_connect(buttonSettings, "clicked", G_CALLBACK(openSettings), NULL);
	g_signal_connect(buttonSave, "clicked", G_CALLBACK(saveMapToFileDialog), NULL);
	gtk_widget_set_sensitive(buttonSave, FALSE);

	GtkWidget *topWidgets[] = {
			buttonDownload, inputLongitude, inputLatitude, inputZoom, inputWidth, inputHeight, buttonSettings, buttonSave }; // tablica wid¿etów w górnym pasku
	for (guint32 i = 0; i < sizeof(topWidgets) / sizeof(GtkWidget*); i++) { //ustawiamy parametry dla wid¿etów w górnym pasku
		gtk_grid_attach(GTK_GRID(gridTop), topWidgets[i], i, 0, 1, 1);
		gtk_widget_set_hexpand(topWidgets[i], TRUE);
	}

	if (nogui) { //jeœli tryb cli to pobieramy mapê bez pokazywania interfejsu
		downloadMap();
	} else {
		gtk_widget_show_all(window);
	}
}

static GOptionEntry entries[] = {
		{
				"nogui", 0, 0, G_OPTION_ARG_NONE, &nogui, "CLI mode", NULL },
		{
				"longitude", 'x', 0, G_OPTION_ARG_DOUBLE, &longitudeCLI, "Geographic longitude", "lf" },
		{
				"latitude", 'y', 0, G_OPTION_ARG_DOUBLE, &latitudeCLI, "Geographic latitude", "lf" },
		{
				"zoom", 'z', 0, G_OPTION_ARG_INT, &zoomCLI, "Zoom", "u" },
		{
				"width", 'w', 0, G_OPTION_ARG_INT, &widthCLI, "Map width", "u" },
		{
				"height", 'h', 0, G_OPTION_ARG_INT, &heightCLI, "Map height", "u" },
		{
				"url", 'u', 0, G_OPTION_ARG_STRING, &urlCLI, "Tiles url", "s" },
		{
				"output", 'o', 0, G_OPTION_ARG_STRING, &outputCLI, "Map output file", "s" },
		{
		NULL
		}
};

void printWorkingDirectory() {
	char cwd[1024];
	if (getcwd(cwd, sizeof(cwd)) != 0) {
		fprintf(stdout, "Current working dir: %s\n", cwd);
	}
}

gint32 main(gint32 argc, gchar **argv) {
	printWorkingDirectory();
	gchar *url = calloc(1001, sizeof(gchar));
	url = DEFAULT_URL;
	settings = (Settings ) {
			TRUE, TRUE, url };
	GtkApplication *app = gtk_application_new("pl.ovsiakov", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

	GError *error = NULL;
	GOptionContext *context = g_option_context_new("- MapViewer");

	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_print("option parsing failed: %s\n", error->message);
		exit(1);
	}

	if (nogui) {
		settings.url = urlCLI;
	}

	gint32 status = g_application_run(G_APPLICATION(app), argc, argv);
	return status;
}
