"""Build a reproducible GUI-only NetAnim presentation profile from installed source."""
from pathlib import Path
import argparse, shutil, subprocess, hashlib, json, difflib, os
ROOT=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser()
ap.add_argument("--source",type=Path,default=Path("/tmp/netanim"))
args=ap.parse_args()
src=args.source.resolve()
work=ROOT/"build/netanim-presentation"
dest=work/"source"
if not (src/"src/animatormode.cpp").is_file(): raise SystemExit("Supply a NetAnim source directory with --source")
if dest.exists(): raise SystemExit("Prepared source exists. Reuse cmake --build build/netanim-presentation/app; do not overwrite local edits.")
dest.parent.mkdir(parents=True,exist_ok=True)
shutil.copytree(src,dest,ignore=shutil.ignore_patterns(".git","build","build-*"))
original={}
def edit(name,fn):
 p=dest/name;original[name]=p.read_text();p.write_text(fn(original[name]))
def change(s,a,b):
 if a not in s: raise RuntimeError("NetAnim source mismatch: "+a[:90])
 return s.replace(a,b)
edit("src/animatorscene.cpp",lambda s:change(s,'''  // Make it square
  qreal minimum = qMin (m_sceneMinPoint.x (), m_sceneMinPoint.y ());
  m_sceneMinPoint = QPointF (minimum, minimum);
  qreal maximum = qMax (m_sceneMaxPoint.x (), m_sceneMaxPoint.y ());
  m_sceneMaxPoint = QPointF (maximum, maximum);''','''  // Presentation viewport only: never change physical node coordinates.
  m_sceneMinPoint = m_minPoint - QPointF(45, 32);
  m_sceneMaxPoint = m_maxPoint + QPointF(60, 40);'''))
def view(s):
 a=s.index("  QTransform transform;",s.index("AnimatorView::updateTransform"))
 b=s.index("\n}",a)
 s=s[:a]+'''  fitInView(AnimatorScene::getInstance()->getBoundaryRect(), Qt::KeepAspectRatio);
  centerOn(AnimatorScene::getInstance()->getBoundaryRect().center());'''+s[b:]
 s=change(s,"  setViewportUpdateMode (BoundingRectViewportUpdate);","  setViewportUpdateMode (FullViewportUpdate);\n  setBackgroundBrush(QColor(\"#f8fafc\"));")
 return s
edit("src/animatorview.cpp",view)
def header(s):
 s=change(s,"  void start ();",'''  void start ();
  void loadPresentationTrace(const QString & filename);
  void capturePresentation(const QString & filename, qreal time);
  void updatePresentationStatus();''')
 s=change(s,"  bool m_showPackets;",'''  bool m_showPackets;
  QCheckBox * m_presentationDataOnly;
  QComboBox * m_presentationFlow;
  QLabel * m_presentationStatus;
  QLabel * m_presentationFile;
  QLabel * m_presentationCrossLegend;''')
 return s
edit("src/animatormode.h",header)
def mode(s):
 s='#include <QCheckBox>\n#include <QRegularExpression>\n#include <QFileInfo>\n'+s
 s=change(s,'  m_showMetaButton->setChecked (true);','  m_showMetaButton->setChecked (false);')
 s=change(s,'  m_showWirelessCirclesButton->setChecked (true);','  m_showWirelessCirclesButton->setChecked (false);')
 # GUI forces the grid on again after loading. Keep it off by default.
 s=s.replace('  m_gridButton->setChecked (true);','  m_gridButton->setChecked (false);')
 s=change(s,'''  m_vLayout->addWidget (m_topToolBar);
  m_vLayout->addWidget (m_mainSplitter);''','''  auto heading = new QLabel("<span style='color:#64748b;font-size:11px;letter-spacing:2px'>ROUTESENSE / NETWORK LAB</span>"
      "<br><span style='font-size:26px;font-weight:700;color:#0f172a'>Routing, made visible.</span>");
  heading->setContentsMargins(22,16,22,16);
  m_vLayout->addWidget(heading);
  m_vLayout->addWidget (m_topToolBar);
  auto presentationPanel = new QWidget;
  presentationPanel->setFixedWidth(270);
  auto panel = new QVBoxLayout(presentationPanel);
  panel->setContentsMargins(22,22,18,18);
  panel->setSpacing(16);
  auto title = new QLabel("EXPERIMENT");
  title->setStyleSheet("color:#64748b;font-size:11px;font-weight:700");
  panel->addWidget(title);
  m_presentationFile = new QLabel("Open a simulation trace");
  m_presentationFile->setWordWrap(true);
  m_presentationFile->setStyleSheet("font-size:16px;font-weight:600;color:#0f172a");
  panel->addWidget(m_presentationFile);
  m_presentationStatus = new QLabel("Real positions. Real trace events.");
  m_presentationStatus->setWordWrap(true);
  m_presentationStatus->setStyleSheet("background:#eef2ff;padding:12px;border-radius:8px;color:#334155");
  panel->addWidget(m_presentationStatus);
  auto flows = new QLabel("<b>FLOW LEGEND</b><br><br>"
      "<span style='color:#2563eb'>● Main flow</span><br>Node 0 → Node 5 · UDP 9");
  flows->setStyleSheet("color:#334155;font-size:12px");
  panel->addWidget(flows);
  m_presentationCrossLegend = new QLabel;
  m_presentationCrossLegend->setWordWrap(true);
  m_presentationCrossLegend->setStyleSheet("color:#334155;font-size:12px");
  panel->addWidget(m_presentationCrossLegend);
  m_presentationFlow = new QComboBox;
  m_presentationFlow->addItems({"All application flows","Main · 0 → 5","Cross 1 · 10 → 4","Cross 2 · 11 → 1"});
  m_presentationFlow->setToolTip("Display filter only; raw XML and simulation events are unchanged");
  panel->addWidget(m_presentationFlow);
  m_presentationDataOnly = new QCheckBox("Application data only");
  m_presentationDataOnly->setChecked(true);
  m_presentationDataOnly->setToolTip("Hide ACK, ARP, routing control and overheard PHY starts. Uncheck to inspect all recorded traffic.");
  panel->addWidget(m_presentationDataOnly);
  connect(m_presentationDataOnly,&QCheckBox::toggled,this,[this](bool){purgeWirelessPackets();});
  connect(m_presentationFlow,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){purgeWirelessPackets();});
  panel->addStretch();
  auto note = new QLabel("<b>READING THIS VIEW</b><br><br>Arrows show recorded PHY reception starts, not confirmed application delivery."
      "<br><br>Node colors identify traffic roles.<br>Hover a node for its IP and role."
      "<br><br>Headers, grid and radio circles are off. Original toolbar controls remain available.");
  note->setWordWrap(true);
  note->setStyleSheet("font-size:11px;color:#64748b");
  panel->addWidget(note);
  m_mainSplitter->addWidget(presentationPanel);
  m_mainSplitter->setStretchFactor(1,1);
  m_mainSplitter->setSizes({45,1100,0,270});
  m_vLayout->addWidget (m_mainSplitter);''')
 s=change(s,"  AnimPropertyBroswer::getInstance ()->postParse ();","  AnimPropertyBroswer::getInstance ()->postParse ();\n  updatePresentationStatus();")
 s=change(s,"  m_currentTime = currentTime;","  m_currentTime = currentTime;\n  updatePresentationStatus();")
 # Filter only viewer events. Native XML, packet timestamps and simulator remain intact.
 s=change(s,"              AnimPacketEvent * packetEvent = static_cast<AnimPacketEvent *> (j->second);",'''              AnimPacketEvent * packetEvent = static_cast<AnimPacketEvent *> (j->second);
              if (m_presentationDataOnly->isChecked()) {
                QRegularExpression udp("ns3::UdpHeader \\\\(length: \\\\d+ \\\\d+ > (9|10|11)\\\\)");
                auto match = udp.match(packetEvent->m_metaInfo);
                if (!match.hasMatch()) break;
                int port = match.captured(1).toInt();
                int focus = m_presentationFlow->currentIndex();
                if (focus && port != 8 + focus) break;
                QRegularExpression ra("DA\\\\(RA\\\\)=([0-9a-fA-F:]+)");
                auto address = ra.match(packetEvent->m_metaInfo);
                if (!address.hasMatch() || !AnimNodeMgr::getInstance()->getNode(packetEvent->m_toId)->hasMac(address.captured(1))) break;
              }''')
 s+=r'''
void netanim::AnimatorMode::updatePresentationStatus()
{
  if (AnimNodeMgr::getInstance()->getCount() < 12) return;
  m_presentationFile->setText(QFileInfo(m_traceFileName).fileName());
  auto n = AnimNodeMgr::getInstance()->getNode(1);
  m_presentationStatus->setText(QString("<b>12 nodes · wireless ad hoc</b><br><br>Time: %1 s<br>Node 1: (%2, %3) m")
      .arg(m_currentTime,0,'f',2).arg(n->getX(),0,'f',0).arg(n->getY(),0,'f',0));
  bool cross = AnimNodeMgr::getInstance()->getNode(10)->getDescription()->toPlainText().contains("Cross");
  m_presentationCrossLegend->setText(cross ?
      "<span style='color:#d97706'>● Cross 1</span><br>Node 10 → Node 4 · UDP 10<br><br>"
      "<span style='color:#7c3aed'>● Cross 2</span><br>Node 11 → Node 1 · UDP 11" :
      "<span style='color:#94a3b8'>Cross traffic disabled</span>");
}
void netanim::AnimatorMode::loadPresentationTrace(const QString & filename)
{
  m_traceFileName = filename;
  if (!parseXMLTraceFile(filename)) { qCritical("Trace load failed"); return; }
  AnimatorScene::getInstance()->setSceneInfoText("",false);
  updatePresentationStatus();
}
void netanim::AnimatorMode::capturePresentation(const QString & filename, qreal time)
{
  setCurrentTime(time);
  updatePresentationStatus();
  AnimatorView::getInstance()->postParse();
  QApplication::processEvents();
  if (!m_centralWidget->window()->grab().save(filename)) qFatal("Cannot save presentation screenshot");
}
'''
 return s
edit("src/animatormode.cpp",mode)
# Compact centered labels follow the real node; only display offsets change.
edit("src/animnode.h",lambda s:change(s,"  void setNodeDescription (QString description);","  void setNodeDescription (QString description);\n  void positionPresentationLabel();"))
def node(s):
 s=change(s,'''  m_nodeDescription->setPlainText (description);''','''  setToolTip(description);
  QString role;
  if (description.contains("main-TX")) role="Main source";
  else if (description.contains("main-RX")) role="Main destination";
  else if (description.contains("X1-TX")) role="Cross 1 source";
  else if (description.contains("X1-RX")) role="Cross 1 destination";
  else if (description.contains("X2-TX")) role="Cross 2 source";
  else if (description.contains("X2-RX")) role="Cross 2 destination";
  else role="Relay";
  if (description.contains("*")) role += " · mobile";
  m_nodeDescription->setHtml(QString("<div style='text-align:center;color:#0f172a;font-family:Sans Serif;font-size:10px'>"
      "<b style='font-size:13px'>Node %1</b><br><span style='color:#64748b'>%2</span></div>").arg(m_nodeId).arg(role));
  QStringList ips;
  for (const auto & ip : getIpv4Addresses()) if (ip != "127.0.0.1") ips << ip;
  setToolTip(QString("Node %1 | %2\\n%3").arg(m_nodeId).arg(role).arg(ips.join(", ")));
  positionPresentationLabel();''')
 s=change(s,"  ResizeableItem::paint (painter, option, widget);","  positionPresentationLabel();\n  ResizeableItem::paint (painter, option, widget);")
 s+=r'''
void netanim::AnimNode::positionPresentationLabel()
{
  qreal scale=1;
  if (scene() && !scene()->views().isEmpty()) scale=qMax(qreal(.1),scene()->views().first()->transform().m11());
  m_nodeDescription->setPos(getX()-m_nodeDescription->boundingRect().width()/(2*scale),
                            getY()+getWidth()/2+5/scale);
}
'''
 return s
edit("src/animnode.cpp",node)
# Flow colors belong to the visual packet, classified from its actual UDP header.
edit("src/animpacket.h",lambda s:change(s,"class AnimPacket :","class AnimPacket :") if False else s.replace("  QString getMeta", "  QString getMeta",1))
# Add a member via a known class declaration, not packet protocol structs.
p=dest/"src/animpacket.h"
s=p.read_text();pos=s.index("class AnimPacket :")
pos=s.index("private:",pos);s=s[:pos]+s[pos:].replace("private:","private:\n  QColor m_presentationColor;",1);p.write_text(s)
def packet(s):
 s=change(s,"  m_infoText = new QGraphicsSimpleTextItem (this);",'''  m_presentationColor = QColor("#94a3b8");
  if (metaInfo.contains("ns3::UdpHeader")) {
    if (metaInfo.contains(" > 9)")) m_presentationColor=QColor("#2563eb");
    else if (metaInfo.contains(" > 10)")) m_presentationColor=QColor("#d97706");
    else if (metaInfo.contains(" > 11)")) m_presentationColor=QColor("#7c3aed");
  }
  m_infoText = new QGraphicsSimpleTextItem (this);''')
 s=change(s,"  p.setColor (Qt::blue);","  p.setColor (m_presentationColor);\n  p.setWidthF(2.0);")
 s=change(s,"  QColor black (0, 0, 5, 130);","  QColor black = m_presentationColor;")
 return s
edit("src/animpacket.cpp",packet)
def main(s):
 s='#include "animatormode.h"\n#include <QTimer>\n#include <QFileInfo>\n'+s
 s=change(s,"  return app.exec ();",'''  app.setStyle("Fusion");
  app.setFont(QFont("Sans Serif",10));
  app.setStyleSheet("QTabWidget::pane{border:0;background:#ffffff}"
      "QTabBar::tab{padding:10px 22px;background:#f1f5f9;color:#64748b;border:0}"
      "QTabBar::tab:selected{background:#ffffff;color:#2563eb;border-bottom:3px solid #2563eb}"
      "QToolBar{background:#ffffff;border:0;padding:5px}"
      "QComboBox{padding:7px;border:1px solid #e2e8f0;border-radius:5px;background:white}"
      "QCheckBox{color:#334155;padding:6px 0}"
      "QToolTip{background:#0f172a;color:white;padding:8px;border:0}");
  auto window=netAnim.getTabWidget();
  window->setWindowTitle("RouteSense | Native NetAnim presentation");
  window->showNormal();
  window->resize(1440,900);
  QString trace=qEnvironmentVariable("ROUTESENSE_TRACE");
  if (argc>1 && !QString(argv[1]).startsWith("-")) trace=QString::fromLocal8Bit(argv[1]);
  if (!trace.isEmpty()) QTimer::singleShot(150,[trace](){
      netanim::AnimatorMode::getInstance()->loadPresentationTrace(trace);
      QString output=qEnvironmentVariable("ROUTESENSE_SCREENSHOT");
      if (!output.isEmpty()) QTimer::singleShot(300,[output](){
          bool ok=false; double time=qEnvironmentVariable("ROUTESENSE_TIME").toDouble(&ok);
          netanim::AnimatorMode::getInstance()->capturePresentation(output,ok?time:0);
          qApp->quit();
      });
  });
  return app.exec ();''')
 return s
edit("src/main.cpp",main)
patch=""
for name,old in original.items():
 patch+="".join(difflib.unified_diff(old.splitlines(True),(dest/name).read_text().splitlines(True),fromfile="a/"+name,tofile="b/"+name))
(ROOT/"tools/netanim-presentation/presentation.patch").write_text(patch)
(ROOT/"tools/netanim-presentation/source-manifest.json").write_text(json.dumps({name:hashlib.sha256(text.encode()).hexdigest() for name,text in original.items()},indent=2))
env=os.environ.copy();env["PATH"]="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
subprocess.run(["cmake","-S",str(dest),"-B",str(work/"app"),"-DCMAKE_BUILD_TYPE=Release"],check=True,env=env)
subprocess.run(["cmake","--build",str(work/"app"),"-j","4"],check=True,env=env)
print("Native presentation binary:",work/"app/netanim")
