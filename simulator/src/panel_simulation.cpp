#include "simulator/panel_simulation.hpp"
#include "simulator/application_catalog_configuration.hpp"
#include "bhp/application_identity.h"
#include "bhp/panel_mode_protocol/codec.h"
#include "vocabulary_panel/fixture_lexinet_client.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <iostream>
#include <regex>
#include <utility>
#include <vector>
#ifdef BHP_SIMULATOR_HTTPS
#include "vocabulary_panel/http_lexinet_client.h"
#include "vocabulary_panel/study_worker.h"
#include "bhp/structured_logger.h"
#endif

namespace bhp::simulator {
using namespace house_panel::panel_ui;
using namespace bhp::panel_mode_protocol;
namespace {
bool review_event(const vocabulary_panel::StudyEvent &event){return event.event_kind=="rated"||event.event_kind=="rating_voided";}
void member(const Json &j,const char *key,const std::set<std::string> &values){if(j.contains(key)&&(!j.at(key).is_string()||!values.count(j.at(key).get<std::string>())))throw std::runtime_error("SIMULATOR.SCENARIO_VALUE_INVALID");}
}

struct PanelSimulation::StudyNetwork {
 // Legacy simulation configs remain visibly fixtures; live sources opt in explicitly.
 bool fixture_data{true};
#ifdef BHP_SIMULATOR_HTTPS
 bhp::StructuredLogger logger{"panel_simulator.study",std::cerr};
 std::shared_ptr<bhp::http_runtime::HttpRuntime> runtime;
 std::shared_ptr<vocabulary_panel::HttpLexinetClient> client;
 std::unique_ptr<vocabulary_panel::StudyWorker> worker;
 vocabulary_panel::StudyCreateRequest creation;
 bool in_flight{false};
 bool review_sync_in_flight{false};
 bool terminal_error{false};
 std::string successor_materialized_for;
 std::chrono::steady_clock::time_point next_attempt{};
 ~StudyNetwork(){worker.reset();if(runtime)static_cast<void>(runtime->shutdown());client.reset();runtime.reset();}
 explicit StudyNetwork(const std::string &path) {
  std::ifstream file(path);if(!file)throw std::runtime_error("SIMULATOR.LEXINET_CONFIG_READ_FAILED");Json j;file>>j;
  fixture_data=j.value("fixture_data",true);
  vocabulary_panel::HttpLexinetConfig config;config.lexinet_base_url=j.at("base_url");config.credential_file_path=j.value("credential_file","");
  config.transport_budget={1000,3000,1048576};config.maximum_snapshot_age_seconds=604800;config.clock_trusted=false;config.allow_qemu_host_test_ca=j.value("allow_qemu_host_test_ca",false);
  if(j.contains("fixture_ca_file")){std::ifstream ca(j.at("fixture_ca_file").get<std::string>());if(!ca)throw std::runtime_error("SIMULATOR.CA_READ_FAILED");config.trusted_ca_certificate_pem_for_tests=std::string(std::istreambuf_iterator<char>(ca),{});}
  auto made=bhp::http_runtime::HttpRuntime::create();if(!made.has_value())throw std::runtime_error(made.error().code);runtime=made.value();client=std::make_shared<vocabulary_panel::HttpLexinetClient>(runtime,config,logger);worker=std::make_unique<vocabulary_panel::StudyWorker>(runtime,client);
  creation.profile_ref=j.at("profile_ref");creation.topic_refs=j.value("topic_refs",std::vector<std::string>{});
 }
#else
 explicit StudyNetwork(const std::string &){throw std::runtime_error("SIMULATOR.HTTPS_NOT_BUILT");}
#endif
};
PanelSimulation::~PanelSimulation()=default;

Json read_scenario(const std::string &path) {
 std::ifstream file(path);if(!file)throw std::runtime_error("SIMULATOR.SCENARIO_READ_FAILED");
 std::string bytes((std::istreambuf_iterator<char>(file)),{});if(bytes.size()>65536)throw std::runtime_error("SIMULATOR.SCENARIO_TOO_LARGE");
 auto j=Json::parse(bytes);
 const std::set<std::string> fields{"schema_version","scenario_id","surface","lifecycle","study_state","agenda_state","presentation_failure","night_red_rgb888","fixture_clock","weather","clock_transition"};
 if(!j.is_object() || j.value("schema_version",0)!=1 || !j.contains("scenario_id") || !j.at("scenario_id").is_string())throw std::runtime_error("SIMULATOR.SCENARIO_SCHEMA_INVALID");
 if(!std::regex_match(j.at("scenario_id").get<std::string>(),std::regex("^[a-z0-9-]{1,64}$")))throw std::runtime_error("SIMULATOR.SCENARIO_ID_INVALID");
 if(j.contains("fixture_clock")&&(!j["fixture_clock"].is_string()||!std::regex_match(j["fixture_clock"].get<std::string>(),std::regex("^([01][0-9]|2[0-3]):[0-5][0-9]$"))))throw std::runtime_error("SIMULATOR.FIXTURE_CLOCK_INVALID");
 if(j.contains("clock_transition")){
  const auto &transition=j.at("clock_transition");
  if(!transition.is_object()||transition.size()!=2||!transition.contains("previous")||!transition.contains("elapsed_ms")
    ||!transition.at("previous").is_string()||!PreviewClock::valid_text(transition.at("previous").get<std::string>())
    ||!transition.at("elapsed_ms").is_number_integer()||transition.at("elapsed_ms")<0||transition.at("elapsed_ms")>PreviewClock::transition_duration_ms)
   throw std::runtime_error("SIMULATOR.CLOCK_TRANSITION_INVALID");
 }
 for(auto it=j.begin();it!=j.end();++it)if(!fields.count(it.key()))throw std::runtime_error("SIMULATOR.SCENARIO_UNKNOWN_FIELD");
 member(j,"surface",{"home","study","agenda"});member(j,"lifecycle",{"day","grace","night","blank","clock_untrusted"});
 member(j,"study_state",{"front","back","loading","empty","offline","stale","error","queued","saved","rejected","provisional","refill","complete","resume","tracking_paused"});
 member(j,"agenda_state",{"today","stale","expired","composer","recording","text_preview","confirmation","delivery_unknown","unavailable"});
 if(j.contains("presentation_failure")&&!j["presentation_failure"].is_boolean())throw std::runtime_error("SIMULATOR.SCENARIO_SCHEMA_INVALID");
 if(j.contains("night_red_rgb888")&&(!j["night_red_rgb888"].is_number_unsigned()||j["night_red_rgb888"].get<unsigned>()>0xffffff))throw std::runtime_error("SIMULATOR.SCENARIO_COLOR_INVALID");
 if(j.contains("weather")){
  const auto &weather=j.at("weather");
  const std::set<std::string> weather_fields{"state","high_celsius","low_celsius","unit"};
  if(!weather.is_object()||!weather.contains("state"))throw std::runtime_error("SIMULATOR.SCENARIO_WEATHER_INVALID");
  for(auto it=weather.begin();it!=weather.end();++it)if(!weather_fields.count(it.key()))throw std::runtime_error("SIMULATOR.SCENARIO_WEATHER_INVALID");
  member(weather,"state",{"available","unavailable","stale"});member(weather,"unit",{"F","C"});
  for(const auto *field:{"high_celsius","low_celsius"})if(weather.contains(field)&&(!weather.at(field).is_number_integer()||weather.at(field)<-80||weather.at(field)>60))throw std::runtime_error("SIMULATOR.SCENARIO_WEATHER_INVALID");
  if(weather.at("state")!="unavailable"&&(!weather.contains("high_celsius")||!weather.contains("low_celsius")||weather.at("high_celsius")<weather.at("low_celsius")))throw std::runtime_error("SIMULATOR.SCENARIO_WEATHER_INVALID");
 }
 return j;
}
Snapshot ScriptedPanelSupervisor::publish(const std::string &state) {
 Snapshot s{std::string(bhp::kApplianceReleaseId),std::string(32,'1'),std::string(32,'2'),++sequence_,"",DisplayMode::DayActive,PresenceState::Present,SolarState::Day,SystemState::Active,80,1790347320000LL};
 if(state=="grace"){s.presence_state=PresenceState::GracePeriod;s.effective_brightness_percent=40;}
 else if(state=="night"){s.display_mode=DisplayMode::NightRed;s.solar_state=SolarState::Night;s.effective_brightness_percent=5;}
 else if(state=="blank"){s.display_mode=DisplayMode::Suspended;s.presence_state=PresenceState::Absent;s.system_state=SystemState::Suspended;s.effective_brightness_percent=0;}
 else if(state=="clock_untrusted"){s.display_mode=DisplayMode::Diagnostic;s.solar_state=SolarState::Unknown;s.effective_brightness_percent=0;}
 else if(state!="day")throw std::runtime_error("SIMULATOR.LIFECYCLE_INVALID");
 auto digest=compute_snapshot_id(s);if(!digest)throw std::runtime_error("SIMULATOR.SNAPSHOT_INVALID");s.snapshot_id=digest.value();return s;
}
void ScriptedPanelSupervisor::observe_pointer(bool down,int x,int y){if(x<0||x>=1024||y<0||y>=600){contact_active_=false;return;}if(down)contact_active_=true;else if(contact_active_){contact_active_=false;if(contacts_.size()<16)contacts_.emplace_back(x,y);}}
std::optional<std::pair<int,int>> ScriptedPanelSupervisor::take_contact(){if(contacts_.empty())return std::nullopt;auto contact=contacts_.front();contacts_.pop_front();return contact;}
PanelSimulation::PanelSimulation(bool window,std::string state_directory,std::string lexinet_configuration_path,std::string application_catalog_path):display(window) {
 application_catalog_=read_application_catalog(application_catalog_path.empty()?std::string(BHP_SIMULATOR_SOURCE_ROOT)+"/panel_ui/config/applications.json":application_catalog_path);
 store_=state_directory.empty()?std::shared_ptr<vocabulary_panel::StudyStore>(std::make_shared<vocabulary_panel::MemoryStudyStore>()):std::shared_ptr<vocabulary_panel::StudyStore>(std::make_shared<vocabulary_panel::FileStudyStore>(state_directory));
 persistent_store_=store_;
 study_=std::make_unique<vocabulary_panel::FlashCardSession>(store_);
 PanelIntentRegistry registry;std::vector<std::string> routes;
 for(const auto &d:registry.definitions())if(d.enabled&&d.intent_id.rfind("study.",0)==0)routes.push_back(d.intent_id);
 routes.push_back("shell.home");
 std::set<std::string> home_route_set{"shell.home","shell.apps_previous","shell.apps_next"};
 for(const auto &app:application_catalog_.applications)if(app.enabled)home_route_set.insert(app.launch_intent);
 const std::vector<std::string> home_routes(home_route_set.begin(),home_route_set.end());
 auto made=ShellController::create({std::chrono::milliseconds(100),"simulation-fixture-not-measured",8,true},std::nullopt,{{"home",home_routes},{"study",routes},{"agenda",{"shell.home","agenda.start_wish_recording","agenda.submit_wish"}}},clock_);
 if(!made)throw std::runtime_error(std::string(made.error().code_string()));controller_=std::move(made).value();
 if(!lexinet_configuration_path.empty()){if(state_directory.empty())throw std::runtime_error("SIMULATOR.HTTPS_REQUIRES_DURABLE_STORE");network_=std::make_unique<StudyNetwork>(lexinet_configuration_path);}
 scene.lifecycle=supervisor.publish("day");
}
void PanelSimulation::seed_study(){
 std::vector<vocabulary_panel::FlashCard> cards;
 for(int i=0;i<8;++i){vocabulary_panel::FlashCard c;c.vocabulary_card_id="fixture-card-"+std::to_string(i);c.word=i%2==0?"serendipity":"resilience";c.definition=i%2==0?scene.card_definition:"The capacity to recover and adapt after difficulty.";c.example_sentence=scene.card_example;c.review_cursor="fixture-cursor";c.session_item_id="fixture-item-"+std::to_string(i);c.content_revision="fixture-revision-1";c.content_sha256=std::string(64,'a');c.source_identity="simulation_fixture";c.position=i;c.selection_reason_json="{}";cards.push_back(c);}
 auto result=study_->install_session(std::move(cards),"simulation-study-session");if(!result.has_value())throw std::runtime_error(result.error().error_code);
 study_->set_clock_trusted(true);
}
void PanelSimulation::load(const Json &j,bool update_lifecycle,bool fixture_override) {
 const auto state=j.value("study_state","front");
 // Synthetic receipts must never acknowledge a restored user's outbox. Validate
 // before restore, input promotion or any other durable state mutation.
 if((state=="queued"||state=="saved"||state=="rejected")&&!fixture_override&&persistent_store_->durable())
  throw std::runtime_error("SIMULATOR.DELIVERY_FIXTURE_REQUIRES_MEMORY");
 fixture_override_=fixture_override;
#ifdef BHP_SIMULATOR_HTTPS
 if(network_){network_->worker->cancel();network_->in_flight=false;network_->review_sync_in_flight=false;network_->terminal_error=false;}
#endif
 store_=fixture_override_?std::shared_ptr<vocabulary_panel::StudyStore>(std::make_shared<vocabulary_panel::MemoryStudyStore>()):persistent_store_;
 const bool network_active=network_&&!fixture_override_;

 scene=PanelScene{scene.lifecycle};scene.applications=application_catalog_;scene.scenario_id=j.at("scenario_id").get<std::string>();scene.surface_id=j.value("surface","study");scene.clock_text=j.value("fixture_clock","07:42");
 preview_clock_enabled_=false;clock_animation_live_=false;
 preview_clock_.reset(scene.clock_text,0,false);
 scene.fixture_data=scene.surface_id!="study"||!network_active||network_->fixture_data;
 if(j.contains("weather")){
  const auto &weather=j.at("weather");
  scene.weather_available=weather.at("state")!="unavailable";scene.weather_stale=weather.at("state")=="stale";
  scene.weather_high_celsius=weather.value("high_celsius",0);scene.weather_low_celsius=weather.value("low_celsius",0);
  scene.weather_temperature_unit=weather.value("unit","F");scene.weather_fixture=true;
 }
 scene.fixture_clock_untrusted=fixture_override_&&j.value("lifecycle","day")=="clock_untrusted";
 scene.agenda_state=j.value("agenda_state","today");scene.night_red_rgb888=j.value("night_red_rgb888",0x180000u);scene.presentation_failure=j.value("presentation_failure",false);
 context_nonce_="simulation-context-"+std::to_string(++context_sequence_);close_voice_context();
 if(update_lifecycle)accept_lifecycle(supervisor.publish(j.value("lifecycle","day")));
 if(!store_->durable())store_=std::make_shared<vocabulary_panel::MemoryStudyStore>();
 study_=std::make_unique<vocabulary_panel::FlashCardSession>(store_);
 if(store_->durable()){auto restored=study_->restore();if(!restored.has_value())throw std::runtime_error(restored.error().error_code);}
 if(study_->snapshot().session_id.empty()&&!network_active)seed_study();
 if(!network_active&&(state=="back" || state=="provisional")) {auto r=study_->apply_intent(vocabulary_panel::StudyIntent::flip,state=="provisional"?vocabulary_panel::StudyInputSource::voice:vocabulary_panel::StudyInputSource::touch,study_->current_binding());if(!r.has_value())throw std::runtime_error(r.error().error_code);}
 if(!network_active&&(state=="queued"||state=="saved"||state=="rejected")) {
  for(const auto intent:{vocabulary_panel::StudyIntent::flip,vocabulary_panel::StudyIntent::rate_good}) {
   auto applied=study_->apply_intent(intent,vocabulary_panel::StudyInputSource::touch,study_->current_binding());
   if(!applied.has_value())throw std::runtime_error(applied.error().error_code);
  }
  if(state!="queued") {
   vocabulary_panel::StudySession fixture;fixture.session_id=study_->snapshot().session_id;fixture.state_version=1;
   vocabulary_panel::FixtureLexinetClient client(std::move(fixture));client.terminal_rejection=state=="rejected";
   auto request=study_->prepare_sync();if(!request.has_value())throw std::runtime_error(request.error().error_code);
   auto response=client.sync(request.value(),"simulation-delivery-fixture");if(!response.has_value())throw std::runtime_error(response.error().error_code);
   auto applied=study_->accept_sync(response.value());if(!applied.has_value())throw std::runtime_error(applied.error().error_code);
  }
 }
 if(state=="offline"||state=="stale")study_->set_connectivity(vocabulary_panel::StudyConnectivity::offline,state=="stale");
 project_study();
 if(state=="loading"||state=="empty"||state=="error"||state=="refill"||state=="complete"||state=="resume"||state=="tracking_paused")scene.study_state=state;
 if(scene.surface_id=="agenda"&&scene.agenda_state=="stale")scene.stale=true;
 if(j.contains("clock_transition")){
  PreviewClock sample;sample.reset(j.at("clock_transition").at("previous"),0,false);sample.change_to(scene.clock_text,0);
  const auto frame=sample.sample(j.at("clock_transition").at("elapsed_ms").get<std::uint64_t>());
  scene.clock_previous_text=frame.previous_text;scene.clock_transition_milli=frame.transition_milli;
 }
 render();
}
void PanelSimulation::switch_surface(const std::string &surface_id){
 if(surface_id==scene.surface_id)return;
 if(scene.surface_id=="study"){
#ifdef BHP_SIMULATOR_HTTPS
  const auto boundary_error=study_->snapshot().error;
#endif
  const auto departed=study_->leave_surface();
  if(!departed.has_value()){scene.acknowledgement=departed.error().error_code;return;}
#ifdef BHP_SIMULATOR_HTTPS
  // leave_surface commits local evidence, which clears transient reducer errors.
  // A terminal server failure remains in force until an explicit scenario reload.
  if(network_&&network_->terminal_error&&boundary_error)study_->set_boundary_error(*boundary_error);
#endif
 }
#ifdef BHP_SIMULATOR_HTTPS
 if(network_){network_->worker->cancel();network_->in_flight=false;network_->review_sync_in_flight=false;}
#endif
 scene.surface_id=surface_id;
 context_nonce_="simulation-context-"+std::to_string(++context_sequence_);close_voice_context();
 project_study();
 scene.fixture_data=surface_id!="study"||!network_||fixture_override_||network_->fixture_data;
 if(surface_id=="agenda")scene.stale=scene.agenda_state=="stale";
 scene.acknowledgement.clear();
}
void PanelSimulation::project_study(){
 const auto &s=study_->snapshot();
 scene.study_state=s.face==vocabulary_panel::CardFace::back?"back":"front";
 if(s.availability!=vocabulary_panel::StudyAvailability::ready){
  scene.study_state=vocabulary_panel::study_availability_name(s.availability);
  if(scene.study_state=="refill_required")scene.study_state=s.connectivity==vocabulary_panel::StudyConnectivity::online?"complete":"refill";
  if(scene.study_state=="resume_required")scene.study_state="resume";
 }
 scene.provisional=s.provisional;scene.can_back=s.can_back;scene.can_undo=s.can_undo;
 scene.offline=s.connectivity==vocabulary_panel::StudyConnectivity::offline;scene.stale=s.stale;
 if(s.flash_card){scene.card_word=s.flash_card->word;scene.card_definition=s.flash_card->definition;scene.card_example=s.flash_card->example_sentence;}
 // Presentation/reveal bookkeeping is quiet. A queued rating or retraction is
 // still unsaved; a receipt, never elapsed time, removes its delivery notice.
 scene.delivery_status.clear();
 if(s.delivery==vocabulary_panel::StudyDelivery::rejected)scene.delivery_status="Not saved";
 else if(std::any_of(study_->pending_events().begin(),study_->pending_events().end(),review_event)){
  scene.delivery_status="Waiting to sync";
#ifdef BHP_SIMULATOR_HTTPS
  if(network_&&!fixture_override_&&network_->in_flight&&network_->review_sync_in_flight)scene.delivery_status="Syncing with Lexinet";
#endif
 }
}
void PanelSimulation::accept_lifecycle(const Snapshot &s){
 if(s.display_mode!=scene.lifecycle.display_mode||s.solar_state==SolarState::Unknown)cancel_clock_animation();
 lifecycle_available_=true;
#ifdef BHP_SIMULATOR_HTTPS
 if(network_&&(s.display_mode!=DisplayMode::DayActive||s.presence_state==PresenceState::Absent)){network_->worker->cancel();network_->in_flight=false;network_->review_sync_in_flight=false;}
#endif
 context_nonce_="simulation-context-"+std::to_string(++context_sequence_);close_voice_context();
 if(network_&&!fixture_override_)project_study();
 scene.lifecycle=s;if(s.display_mode!=DisplayMode::DayActive)scene.acknowledgement.clear();}
void PanelSimulation::close_voice_context(){
 if(voice_){
  try{voice_->send({{"op","context"},{"context",{{"display_mode","suspended"},{"presence",false},{"surface",scene.surface_id},{"idle",false},{"muted",true},{"consumer_connected",lifecycle_available_},{"active_context_nonce",context_nonce_},{"render_generation",render_generation},{"back_revealed",false},{"history_available",false}}}});}catch(const std::exception &){voice_.reset();}
 }
 voice_pending_=false;auto ignored=controller_->update_voice_binding(std::nullopt);(void)ignored;
}
void PanelSimulation::invalidate_lifecycle(){
 cancel_clock_animation();
 lifecycle_available_=false;
#ifdef BHP_SIMULATOR_HTTPS
 if(network_){network_->worker->cancel();network_->in_flight=false;network_->review_sync_in_flight=false;}
#endif
 voice_.reset();voice_pending_=false;
 auto ignored=controller_->update_voice_binding(std::nullopt);(void)ignored;
 if(network_&&!fixture_override_)project_study();
 scene.acknowledgement.clear();render();
}
std::uint64_t PanelSimulation::render(){auto visible=scene;if(!lifecycle_available_)visible.presentation_failure=true;render_generation=display.render(visible);return render_generation;}
void PanelSimulation::semantic_intent(const std::string &id,const std::string &source){
 if(source=="voice"){scene.acknowledgement="Use the fake recognition input";render();return;}
 clock_.advance();const auto kind=source=="touch"?IntentSourceKind::Touch:IntentSourceKind::HostFixture;
 std::optional<VoiceIntentEvidence> evidence;
 InteractionIntentEnvelope intent{1,context_nonce_,"simulation-"+source,kind,++source_sequence_,source_sequence_*1000000000,render_generation,scene.surface_id,id,evidence};
 route_intent(intent);
}
void PanelSimulation::route_intent(const InteractionIntentEnvelope &intent){
 const auto &id=intent.intent_id;const auto kind=intent.source_kind;
 auto result=controller_->process_intent(intent,{context_nonce_,scene.surface_id,scene.lifecycle.display_mode,scene.presentation_failure||scene.fixture_clock_untrusted||!lifecycle_available_?ShellTransitionState::Loading:ShellTransitionState::Stable,render_generation});
 scene.acknowledgement=result.disposition==IntentDisposition::Accepted?"Action accepted":result.disposition==IntentDisposition::Duplicate?"Already handled":result.disposition==IntentDisposition::Conflict?"Please try again":"Action unavailable";
 if(result.disposition==IntentDisposition::Accepted){
  const PanelIntentRegistry registry;
  const auto *definition=registry.find(id);
  if(definition&&!definition->navigation_surface_id.empty()){switch_surface(definition->navigation_surface_id);render();return;}
  if(id=="shell.apps_previous"||id=="shell.apps_next"){
   const auto page=application_grid_page(scene.applications,scene.home_page);
   if(id=="shell.apps_next"&&scene.home_page+1<page.page_count)++scene.home_page;
   else if(id=="shell.apps_previous"&&scene.home_page>0)--scene.home_page;
   else scene.acknowledgement=id=="shell.apps_next"?"Last page":"First page";
   context_nonce_="simulation-context-"+std::to_string(++context_sequence_);close_voice_context();render();return;
  }
  const std::map<std::string,vocabulary_panel::StudyIntent> mapping{{"study.flip",vocabulary_panel::StudyIntent::flip},{"study.next",vocabulary_panel::StudyIntent::next},{"study.back",vocabulary_panel::StudyIntent::back},{"study.rate.again",vocabulary_panel::StudyIntent::rate_again},{"study.rate.hard",vocabulary_panel::StudyIntent::rate_hard},{"study.rate.good",vocabulary_panel::StudyIntent::rate_good},{"study.rate.easy",vocabulary_panel::StudyIntent::rate_easy},{"study.rating.undo",vocabulary_panel::StudyIntent::rating_undo}};
  auto it=mapping.find(id);if(it!=mapping.end()){
   auto semantic=scene.study_state=="resume"?vocabulary_panel::StudyIntent::resume:it->second;
   const bool first_card_boundary=semantic==vocabulary_panel::StudyIntent::back&&!study_->snapshot().can_back;
   auto applied=study_->apply_intent(semantic,kind==IntentSourceKind::Voice?vocabulary_panel::StudyInputSource::voice:vocabulary_panel::StudyInputSource::touch,study_->current_binding());
   if(!applied.has_value())scene.acknowledgement=applied.error().error_code;
   else if(first_card_boundary)scene.acknowledgement="At the first card";
   project_study();
  }
 }
 render();
}
void PanelSimulation::tap(int x,int y){if(scene.presentation_failure)return;for(auto it=display.hit_targets().rbegin();it!=display.hit_targets().rend();++it)if(x>=it->x&&x<it->x+it->width&&y>=it->y&&y<it->y+it->height){auto id=it->intent_id;semantic_intent(id,"touch");return;}}
void PanelSimulation::event(const Json &e){const auto type=e.at("type").get<std::string>();if(type=="scenario")load(read_scenario(e.at("path")),true,true);else if(type=="lifecycle"){accept_lifecycle(supervisor.publish(e.at("state")));render();}else if(type=="intent")semantic_intent(e.at("intent_id"),e.value("source","host_fixture"));else if(type=="tap"){supervisor.observe_pointer(true,e.at("x"),e.at("y"));supervisor.observe_pointer(false,e.at("x"),e.at("y"));auto c=supervisor.take_contact();if(c)tap(c->first,c->second);}else if(type=="screenshot")display.write_png(e.at("path"));else if(type=="utterance")utterance(e.at("phrase"));else if(type=="clock_preview")enable_preview_clock(e.at("enabled").get<bool>());else if(type=="clock_step")advance_preview_clock();else if(type=="quit")quit=true;else throw std::runtime_error("SIMULATOR.EVENT_UNKNOWN");}
void PanelSimulation::service(){display.service();pump_study();pump_voice();pump_clock();}
namespace {
std::uint64_t clock_monotonic_ms(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
}
void PanelSimulation::enable_preview_clock(bool enabled){
 preview_clock_enabled_=enabled;clock_animation_live_=false;last_clock_sample_ms_=clock_monotonic_ms();
 preview_clock_.reset(scene.clock_text,last_clock_sample_ms_,enabled);cancel_clock_animation();render();
}
void PanelSimulation::advance_preview_clock(){
 if(scene.clock_source!="fixture")throw std::runtime_error("SIMULATOR.CLOCK_PREVIEW_REQUIRED");
 const auto now=clock_monotonic_ms();
 preview_clock_.reset(scene.clock_text,now,preview_clock_enabled_);
 preview_clock_.change_to(PreviewClock::next_minute(scene.clock_text),now);clock_animation_live_=true;
 scene.clock_previous_text=preview_clock_.frame().previous_text;scene.clock_text=preview_clock_.frame().text;
 scene.clock_transition_milli=0;last_clock_sample_ms_=now;render();
}
void PanelSimulation::cancel_clock_animation(){
 preview_clock_.cancel_transition();clock_animation_live_=false;scene.clock_previous_text.clear();scene.clock_transition_milli=1000;
}
void PanelSimulation::pump_clock(){
 if(!preview_clock_enabled_&&!clock_animation_live_)return;
 const auto now=clock_monotonic_ms();if(now-last_clock_sample_ms_<33)return;last_clock_sample_ms_=now;
 const bool visible=lifecycle_available_&&!scene.presentation_failure&&!scene.fixture_clock_untrusted
  &&scene.clock_source=="fixture"&&scene.lifecycle.solar_state!=SolarState::Unknown
  &&scene.lifecycle.system_state==SystemState::Active
  &&(scene.lifecycle.display_mode==DisplayMode::DayActive||scene.lifecycle.display_mode==DisplayMode::NightRed);
 const auto frame=preview_clock_.sample(now,visible);
 const bool changed=frame.text!=scene.clock_text||frame.previous_text!=scene.clock_previous_text||frame.transition_milli!=scene.clock_transition_milli;
 scene.clock_text=frame.text;scene.clock_previous_text=frame.previous_text;scene.clock_transition_milli=frame.transition_milli;
 clock_animation_live_=!frame.previous_text.empty();if(changed&&visible)render();
}
void PanelSimulation::pump_study(){
#ifdef BHP_SIMULATOR_HTTPS
 if(!network_||fixture_override_||!lifecycle_available_)return;
 if(auto completion=network_->worker->take_completion()){
  network_->in_flight=false;network_->review_sync_in_flight=false;
  if(completion->error){study_->set_boundary_error(*completion->error);scene.acknowledgement=completion->error->error_code;network_->terminal_error=!completion->error->retryable;}
  else if(completion->session){const auto previous=study_->snapshot().session_id;auto applied=study_->install_session(*completion->session);if(!applied.has_value()){study_->set_boundary_error(applied.error());scene.acknowledgement=applied.error().error_code;network_->terminal_error=!applied.error().retryable;}else if(!previous.empty())network_->successor_materialized_for=previous;}
  else if(completion->synchronization){auto applied=study_->accept_sync(*completion->synchronization);if(!applied.has_value()){study_->set_boundary_error(applied.error());scene.acknowledgement=applied.error().error_code;network_->terminal_error=!applied.error().retryable;}}
  project_study();render();network_->next_attempt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
 }
 if(network_->terminal_error||network_->in_flight||std::chrono::steady_clock::now()<network_->next_attempt||scene.surface_id!="study"||scene.lifecycle.display_mode!=DisplayMode::DayActive||scene.lifecycle.presence_state==PresenceState::Absent)return;
 study_->set_clock_trusted(scene.lifecycle.solar_state!=SolarState::Unknown);
 if(study_->snapshot().session_id.empty()){
  auto creation=study_->prepare_create(network_->creation.profile_ref,network_->creation.topic_refs);
  if(creation.has_value()){auto started=network_->worker->create_or_resume(creation.value(),"simulation-create");network_->in_flight=started.has_value();}
  else {study_->set_boundary_error(creation.error());network_->terminal_error=!creation.error().retryable;project_study();render();}
 }else{
  if(study_->snapshot().availability==vocabulary_panel::StudyAvailability::refill_required&&network_->successor_materialized_for!=study_->snapshot().session_id){
   auto successor=study_->prepare_successor();
   if(successor.has_value()){auto started=network_->worker->create_or_resume(successor.value(),"simulation-successor");network_->in_flight=started.has_value();network_->next_attempt=std::chrono::steady_clock::now()+std::chrono::seconds(1);return;}
  }
  if(study_->needs_sync()){
   auto sync=study_->prepare_sync();
   if(sync.has_value()){
    auto started=network_->worker->sync(sync.value(),"simulation-sync");network_->in_flight=started.has_value();
    network_->review_sync_in_flight=network_->in_flight&&std::any_of(sync.value().events.begin(),sync.value().events.end(),review_event);
    const auto previous=scene.delivery_status;project_study();if(scene.delivery_status!=previous)render();
   }
  }
 }
 network_->next_attempt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
#endif
}
void PanelSimulation::utterance(const std::string &phrase){
 if(voice_pending_){scene.acknowledgement="Recognition busy";return;}
 try{
  if(!voice_)voice_=std::make_unique<FakeVoiceConnection>(BHP_SIMULATOR_SOURCE_ROOT);
  const auto &study=study_->snapshot();
  Json context{{"display_mode",to_string(scene.lifecycle.display_mode)},{"presence",scene.lifecycle.presence_state==PresenceState::Present},{"surface",scene.surface_id},{"idle",lifecycle_available_&&!scene.presentation_failure&&!scene.fixture_clock_untrusted},{"muted",false},{"consumer_connected",lifecycle_available_},{"active_context_nonce",context_nonce_},{"render_generation",render_generation},{"back_revealed",study.face==vocabulary_panel::CardFace::back},{"history_available",study.can_back}};
  voice_->send({{"op","context"},{"context",context}});
  voice_->send({{"op","utterance"},{"phrase",phrase},{"utterance_sequence",++utterance_sequence_}});voice_pending_=true;
 }catch(const std::exception &){voice_.reset();voice_pending_=false;scene.acknowledgement="Voice unavailable - touch remains ready";render();}
}
void PanelSimulation::pump_voice(){
 if(!voice_)return;
 try{for(int budget=0;budget<4;++budget){auto response=voice_->receive();if(!response)break;
  if(!response->value("ok",false)){voice_pending_=false;scene.acknowledgement="Voice abstained - no action";render();continue;}
  if(response->contains("binding")){
   if(response->at("binding").is_null()){auto ignored=controller_->update_voice_binding(std::nullopt);(void)ignored;continue;}
   const auto &b=response->at("binding");VoiceCatalogBinding binding{b.at("catalog_revision"),b.at("active_command_set_digest"),b.at("backend_artifact_digest"),b.at("recognizer_build_model_identity"),b.at("recognition_policy_digest"),{}};
   for(const auto &mapping:b.at("spoken_form_bindings"))binding.spoken_form_bindings.push_back({mapping.at("spoken_form_id"),mapping.at("intent_id")});
   auto updated=controller_->update_voice_binding(binding);if(!updated)throw std::runtime_error("SIMULATOR.VOICE_BINDING_INVALID");
  }
  if(response->contains("intent")){voice_pending_=false;const auto &i=response->at("intent"),&v=i.at("voice_evidence");
   InteractionIntentEnvelope envelope{i.at("protocol_version"),i.at("active_context_nonce"),i.at("source_instance_id"),IntentSourceKind::Voice,i.at("source_sequence"),i.at("observed_monotonic_time_ns"),i.at("render_generation"),i.at("target_surface_id"),i.at("intent_id"),VoiceIntentEvidence{v.at("spoken_form_id"),v.at("catalog_revision"),v.at("active_command_set_digest"),v.at("backend_artifact_digest"),v.at("recognizer_build_model_identity"),v.at("recognition_policy_digest")}};
   clock_.advance();route_intent(envelope);
  }
 }}catch(const std::exception &){voice_.reset();voice_pending_=false;auto ignored=controller_->update_voice_binding(std::nullopt);(void)ignored;scene.acknowledgement="Voice unavailable - touch remains ready";render();}
}
Json PanelSimulation::status()const{return {{"scenario_id",scene.scenario_id},{"surface",scene.surface_id},{"home_page",scene.home_page},{"home_page_count",application_grid_page(scene.applications,scene.home_page).page_count},{"render_generation",render_generation},{"display_mode",to_string(scene.lifecycle.display_mode)},{"brightness_percent",scene.presentation_failure||!lifecycle_available_?0:scene.lifecycle.effective_brightness_percent},{"presentation_result",scene.presentation_failure||!lifecycle_available_?"failed":"simulated_commit"},{"lifecycle_available",lifecycle_available_},{"study_state",scene.study_state},{"delivery_status",scene.delivery_status},{"provisional",scene.provisional},{"fixture_data",scene.fixture_data},{"pending_review_events",std::count_if(study_->pending_events().begin(),study_->pending_events().end(),review_event)},{"acknowledgement",scene.acknowledgement},{"fixture_override",fixture_override_},{"fixture_clock_untrusted",scene.fixture_clock_untrusted},{"clock_text",scene.clock_text},{"clock_source",scene.clock_source},{"clock_previous_text",scene.clock_previous_text},{"clock_transition_milli",scene.clock_transition_milli},{"clock_preview_running",preview_clock_enabled_},{"weather_available",scene.weather_available},{"weather_stale",scene.weather_stale},{"weather_fixture",scene.weather_fixture},{"weather_temperature_unit",scene.weather_temperature_unit},{"simulation_not_hardware_evidence",true}};}
}
