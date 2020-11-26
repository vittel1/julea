# Container

## Docker

Root-Rechte auf dem System werden benötigt.

### Docker Image

Im Root Verzeichnis des Repository liegt ein Beispiel Dockerfile, dass die Standard Dependencies lädtund JULEA danach konfiguriert mittels Meson und Ninja. Das Dockerfile kann angepasst werden und dann ein neues Images von JULEA erstellt werden. Wichtig hier ist der Pfad: JULEA wird standardmäßig nach /julea kopiert.
```
docker build -t julea-standard:1.0 .
```

Das Image kann potentiel auf Dockerhub veröffentlich werden. Dann muss wie folgt vorgegangen werden.
```
docker tag julea-standard:1.0 <DOCKER ID>/julea-standard:1.0
docker push <DOCKER ID>/julea-standard:1.0
```

### Use Docker Image

In dem Verzeichnis ./container/docker gibt es Beispiele für die Benutzung von JULEA. Zum einen für einen JULEA-Server und zum anderen für einen JULEA-Client.

#### Network
Wissen über das Networking von Docker sind notwendig.
Docs: https://docs.docker.com/config/containers/container-networking/

#### Network Communication between containers
Create your own network. Diese Network müssen dann alle Container, die miteinander kommunizieren wollen, mitbekommen.

```
docker network create myNet
```

#### Server

Der Server beutzt im ersten Schritt das Image aus dem ersten Kapitel. Im gleichen Ordner wird ein Skript angelegt, durch das der Server konfiguriert wird. Lesen Sie das nächste Kapitel für genauere Hintergrund Informationen. Das Skript setzt die Konfiguration von JULEA fest mittel julea-config und startet anschließend einen Server. Außerdem wird in die .bashrc das Laden der Umgebumgsvariablen festgesetzt, damit, wenn man innerhalb des Containers arbeitet, die Befehle von JULEA zur Verfügung stehen. Im Dockerfile das entsprechende Skript wird in das Image reinkopiert. Der Expose Befehl zeigt an, welcher Port standardmäßig für den Server vergeben wird. Dies ist mehr zu dokumentation zwecken. Abschließend wird ein Entrypoint festgelegt, der aufgerufen wird, wenn docker run ausgeführt wird, und mittels CMD-Befehl werden die default Werte für den Server gesetzt. Hier der Servername und die Portnummer. Diese werden an das Skript übergeben.

```
cd ./container/docker/server
docker build -t julea-server-image .
docker run -it -d --name julea-server julea-server-image
docker exec -ti julea-server /bin/bash
```

oder falls die Default Werte überschrieben werden sollen
```
docker run -it -d --name julea-server julea-server-image SERVERNAME PORT
```

für die Network Communication
```
docker run -it -d --network-alias juleaServer -p 9876:9876/tcp --network myNet --name julea-server julea-server-image
```

#### Client
Beim Client wird im Grunde die gleiche Vorgehensweise verwendet. Hier wird im CMD-Befehl des Dockerfiles der Hostname des JULEA-Servers angegeben, sowie die entsprechende Portnummer. Falls die Default Werte überschrieben werden, muss das im Client auch passieren. Im Skript wird am Ende ein Tail Befehl ausgeführt, der dafür sorgt, dass der Container nach dem Starten nicht sofort wieder heruntergefahren wird. Das würde normalerweise passieren, weil kein dauerhaft Prozess läuft.

```
cd ./container/docker/client
docker build -t julea-client-image .
docker run -it -d --name julea-client julea-client-image
docker exec -ti julea-client /bin/bash
```

oder falls die Default Werte überschrieben werden sollen
```
docker run -it -d --name julea-client julea-client-image SERVERNAME PORT
```

für die Network Communication
```
docker run -it -d --network myNet --name julea-client julea-client-image
```

#### Why to use ENTRYPOINT with a Script
Im Dockerfile des Server steht nur der Entrypoint, der auf ein Skript verweist. Innerhalb des Skripts wird die Konfiguration für JULEA mittel julea-config vorgenommen und der Server gestartet. Jetzt könnte man sich fragen, warum diese Konfiguration nicht direkt im Dockerfile gesetzt wird. Dazu muss man wissen, dass jeder RUN Befehl im Dockerfile eine eigenen Layer erzeugt. Die Umgebungsvariablen, die mittels des environment-Skriptes gesetzt werden, sind nicht persistent. Das heißt, dass ein anderer Layer nicht mehr auf diese zugreifen kann, was dazu führt, dass mit zwei Run-Statements der Befehl julea-config nicht mehr verfügbar ist und es zu einem Fehler kommt. Ein Entrypoint-Befehl, der z.B. einen JULEA-Server starten könnten, hat ein ähnliches Problem. Auch hier kann nicht auf die Umgebungsvariablen zugegriffen werden, diese müssten erneut gesetzt werden, um den Befehl julea-server zu finden. Die gesamte Konfiguration innerhalb des Docker Containers zu machen, wird letztendlich viel zu kompliziert und unübersichtlicht. Deshalb wird empfohlen die Konfigiration auf ein externe Skript auszulagern.
Doc: https://goinbigdata.com/docker-run-vs-cmd-vs-entrypoint/

#### Volume
Um dynamischer Entwickeln zu können, gibt es die Möglichkeit lokale Verzeichnisse in den Container rein zu mounten. Somit könnte man auf dem lokalen Host an JULEA entwickeln oder benutzen und das Ergebnis dann in dem Container kompilieren und ausführen.
Als Beispiel wird der Client hier erweitert. Im Client Verzeichnis liegt ein Ordner build in dem die Beispielfiles aus dem JULEA Projekt liegen. Diese werden in den Container gemountet. Im Container wird das Verzeichnis dann unter /build verfügbar sein.

```
docker run -it -d --network myNet -v /absolute/path/to/dir/build:/build --name julea-client julea-client-image
```

#### Docker-Compose
Um die Verwaltung von Container zu vereinfachen, gibt es im Root-Verzeichnis von JULEA ein docker-compose Beispiel. In diesem wird ein Server und ein Client erstellt.
Der Name des Service von juleaServer ist der Servername, der für den Client eingetragen werden muss, ansonsten kann der Server vom Client aus nicht erreicht werden. Für den Server werden hier noch die Ports definiert, die nach außen und für andere Container sichtbar sind.
Zudem wird für beide Container ein Name für das Image und einer für den Container festgelegt, damit keine zufälligen generiert werden und eindeutig bleiben.
Für den Client wurde mit dem Volume wieder, wie im einzel Client Beispiel, ein Verzeichnis vom Host in den Container gemountet.
Das erstellen des networks ist nicht mehr notwendig, da docker-compose das für einen übernimmt. Für das Beispiel wird ein Network erstellt, das julea_default heißt. Dieses lässt sich mit docker network inspect julea_default untersuchen. In diesem sind dann beide Beispiel Container zu finden.

## Singularity

## Podman
