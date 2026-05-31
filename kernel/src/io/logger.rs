
use log::{Level, LevelFilter, Log, Metadata, Record};

struct SimpleLogger;

static LOGGER: SimpleLogger = SimpleLogger;




impl Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= log::max_level()
    }

    fn log(&self, record: &Record) {
        if !self.enabled(record.metadata()) {
            return;
        }

        let color = match record.level() {
            Level::Debug => 32, // green
            Level::Error => 31, // red
            Level::Warn => 93,  // yellow
            Level::Trace => 90, // gray
            Level::Info => 36,  // aqua

        };

        crate::println!(
            "\x1b[{}m[{:>5}] {}\x1b[0m",
            color,
            record.level(),
            record.args(),
        );
    }

    fn flush(&self) {}
}

pub fn init() {
    if log::set_logger(&LOGGER).is_ok() {
        log::set_max_level(log_level_from_env());
    }
}

fn log_level_from_env() -> LevelFilter {
    match option_env!("LOG") {
        Some("ERROR") | Some("error") => LevelFilter::Error,
        Some("WARN") | Some("warn") => LevelFilter::Warn,
        Some("INFO") | Some("info") => LevelFilter::Info,
        Some("DEBUG") | Some("debug") => LevelFilter::Debug,
        Some("TRACE") | Some("trace") => LevelFilter::Trace,
        _ => LevelFilter::Warn,
    }
}