import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faArrowRightLong } from '@fortawesome/free-solid-svg-icons/faArrowRightLong';
import { faBluetoothB } from '@fortawesome/free-brands-svg-icons/faBluetoothB';
import { faChartSimple } from '@fortawesome/free-solid-svg-icons/faChartSimple';
import { faGaugeHigh } from '@fortawesome/free-solid-svg-icons/faGaugeHigh';
import { faLayerGroup } from '@fortawesome/free-solid-svg-icons/faLayerGroup';
import { faMugHot } from '@fortawesome/free-solid-svg-icons/faMugHot';
import { faWaveSquare } from '@fortawesome/free-solid-svg-icons/faWaveSquare';

const pillars = [
  {
    eyebrow: 'Live machine state',
    title: 'Track temperature, pressure, and brew mode in one calm surface.',
    body: 'GaggiMate turns scattered espresso telemetry into a control room that stays readable during the shot.',
    icon: faGaugeHigh,
  },
  {
    eyebrow: 'Profiles and beans',
    title: 'Keep recipes, beans, and machine behavior tied together.',
    body: 'Build repeatable workflows around the coffee you are actually using instead of juggling notes in separate tools.',
    icon: faLayerGroup,
  },
  {
    eyebrow: 'Shot review',
    title: 'Study history, analyze curves, and spot what changed.',
    body: 'The analyzer and statistics views make it easier to compare extractions and tune with confidence.',
    icon: faChartSimple,
  },
];

const appHighlights = [
  'Responsive control dashboard for phone, tablet, and desktop',
  'Dedicated shot analyzer with overlays and notes',
  'Bean-aware profile management for repeatable dialing in',
  'Bluetooth scale support and OTA settings workflow',
];

export function LandingPage() {
  return (
    <div className='landing-page'>
      <section className='landing-hero overflow-hidden px-4 pb-14 pt-8 sm:px-6 lg:px-10 lg:pb-20 lg:pt-12'>
        <div className='landing-hero-grid mx-auto grid w-full max-w-7xl gap-10 lg:grid-cols-[minmax(0,1.05fr)_minmax(22rem,0.95fr)] lg:items-end'>
          <div className='max-w-2xl'>
            <div className='landing-badge'>
              <span className='landing-badge-dot' />
              Open espresso control surface
            </div>
            <div className='mt-6 space-y-5'>
              <p className='font-logo text-[clamp(2.8rem,9vw,6.6rem)] leading-none tracking-[0.16em] text-white'>
                GAGGIMATE
              </p>
              <h1 className='max-w-3xl text-4xl font-bold leading-tight text-white sm:text-5xl lg:text-6xl'>
                A web app for running, reading, and refining your machine from anywhere.
              </h1>
              <p className='max-w-xl text-base leading-7 text-white/72 sm:text-lg'>
                Give your Gaggia workflow a public front door, then step into a focused live app
                for dashboards, profiles, shot history, and espresso analysis.
              </p>
            </div>

            <div className='mt-8 flex flex-col gap-3 sm:flex-row'>
              <a href='/app' className='landing-primary-cta'>
                Open live app
                <FontAwesomeIcon icon={faArrowRightLong} />
              </a>
              <a href='https://github.com/jniebuhr/gaggimate' className='landing-secondary-cta'>
                View GitHub project
              </a>
            </div>

            <div className='mt-10 grid gap-4 text-white/80 sm:grid-cols-3'>
              <div>
                <div className='text-3xl font-semibold text-white'>Live</div>
                <p className='mt-2 text-sm leading-6'>
                  WebSocket-powered status surface for an active machine session.
                </p>
              </div>
              <div>
                <div className='text-3xl font-semibold text-white'>Review</div>
                <p className='mt-2 text-sm leading-6'>
                  Analyzer, history, and statistics views for understanding every shot.
                </p>
              </div>
              <div>
                <div className='text-3xl font-semibold text-white'>Portable</div>
                <p className='mt-2 text-sm leading-6'>
                  Deploy the frontend once and access the public entry point from any device.
                </p>
              </div>
            </div>
          </div>

          <div className='landing-visual-panel'>
            <div className='landing-visual-orbit landing-visual-orbit-left' />
            <div className='landing-visual-orbit landing-visual-orbit-right' />

            <div className='landing-visual-card landing-visual-card-main'>
              <div className='landing-visual-topline'>
                <span className='landing-dot-cluster'>
                  <span />
                  <span />
                  <span />
                </span>
                <span>Remote brew surface</span>
              </div>

              <div className='mt-6 grid gap-4 md:grid-cols-[1.15fr_0.85fr]'>
                <div className='space-y-4'>
                  <div>
                    <div className='text-[0.68rem] uppercase tracking-[0.28em] text-white/45'>
                      Extraction status
                    </div>
                    <div className='mt-2 flex items-end gap-3'>
                      <div className='text-5xl font-semibold leading-none text-white'>93.4</div>
                      <div className='pb-1 text-sm uppercase tracking-[0.24em] text-white/55'>
                        C
                      </div>
                    </div>
                  </div>

                  <div className='landing-chart'>
                    <span className='landing-chart-line landing-chart-line-hot' />
                    <span className='landing-chart-line landing-chart-line-flow' />
                    <span className='landing-chart-line landing-chart-line-pressure' />
                  </div>

                  <div className='grid grid-cols-3 gap-3'>
                    <div className='landing-metric-block'>
                      <div className='text-[0.65rem] uppercase tracking-[0.25em] text-white/45'>
                        Pressure
                      </div>
                      <div className='mt-2 text-xl font-semibold text-white'>8.6 bar</div>
                    </div>
                    <div className='landing-metric-block'>
                      <div className='text-[0.65rem] uppercase tracking-[0.25em] text-white/45'>
                        Flow
                      </div>
                      <div className='mt-2 text-xl font-semibold text-white'>2.1 ml/s</div>
                    </div>
                    <div className='landing-metric-block'>
                      <div className='text-[0.65rem] uppercase tracking-[0.25em] text-white/45'>
                        Yield
                      </div>
                      <div className='mt-2 text-xl font-semibold text-white'>36 g</div>
                    </div>
                  </div>
                </div>

                <div className='space-y-3'>
                  <div className='landing-side-tile'>
                    <FontAwesomeIcon icon={faWaveSquare} className='text-lg text-[#ff9c54]' />
                    <div>
                      <div className='text-sm font-semibold text-white'>Shot analyzer</div>
                      <p className='mt-1 text-sm leading-6 text-white/60'>
                        Overlay curves and attach tasting notes after every pull.
                      </p>
                    </div>
                  </div>
                  <div className='landing-side-tile'>
                    <FontAwesomeIcon icon={faBluetoothB} className='text-lg text-[#7bc7ff]' />
                    <div>
                      <div className='text-sm font-semibold text-white'>Scale integrations</div>
                      <p className='mt-1 text-sm leading-6 text-white/60'>
                        Keep weight-aware workflows connected during dialing and review.
                      </p>
                    </div>
                  </div>
                  <div className='landing-side-tile'>
                    <FontAwesomeIcon icon={faMugHot} className='text-lg text-[#ffd28a]' />
                    <div>
                      <div className='text-sm font-semibold text-white'>Bean memory</div>
                      <p className='mt-1 text-sm leading-6 text-white/60'>
                        Tie recipes, beans, and machine behavior together in one place.
                      </p>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <div className='landing-visual-card landing-visual-card-floating'>
              <div className='text-[0.68rem] uppercase tracking-[0.28em] text-white/45'>
                Why it works
              </div>
              <div className='mt-3 space-y-2 text-sm leading-6 text-white/68'>
                {appHighlights.map(item => (
                  <div key={item} className='flex gap-2'>
                    <span className='mt-[0.55rem] inline-flex size-1.5 rounded-full bg-[#ff9c54]' />
                    <span>{item}</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </div>
      </section>

      <section className='px-4 py-14 sm:px-6 lg:px-10'>
        <div className='mx-auto grid w-full max-w-7xl gap-12 lg:grid-cols-[0.7fr_1.3fr] lg:items-start'>
          <div className='space-y-4'>
            <div className='landing-section-label'>Built for the full brew loop</div>
            <h2 className='text-3xl font-bold leading-tight text-base-content sm:text-4xl'>
              The public site introduces the product. The app stays focused on the machine.
            </h2>
            <p className='max-w-md text-base leading-7 text-base-content/68'>
              That split means the deployed site feels polished to anyone visiting it, while the
              connected controls still live in a dedicated workspace for owners.
            </p>
          </div>

          <div className='grid gap-5 lg:grid-cols-3'>
            {pillars.map(item => (
              <article key={item.title} className='landing-feature-panel'>
                <div className='landing-feature-icon'>
                  <FontAwesomeIcon icon={item.icon} />
                </div>
                <div className='mt-5 text-[0.7rem] font-semibold uppercase tracking-[0.26em] text-base-content/45'>
                  {item.eyebrow}
                </div>
                <h3 className='mt-4 text-xl font-semibold leading-8 text-base-content'>
                  {item.title}
                </h3>
                <p className='mt-4 text-sm leading-7 text-base-content/62'>{item.body}</p>
              </article>
            ))}
          </div>
        </div>
      </section>

      <section className='px-4 pb-20 sm:px-6 lg:px-10'>
        <div className='landing-cta-shell mx-auto max-w-7xl'>
          <div className='max-w-2xl'>
            <div className='landing-section-label'>Ready to explore</div>
            <h2 className='mt-4 text-3xl font-bold leading-tight text-white sm:text-4xl'>
              Enter the control app, connect to your machine, and start brewing with context.
            </h2>
            <p className='mt-4 max-w-xl text-base leading-7 text-white/70'>
              The public deployment gives you an always-available starting point, and the live app
              remains one click away when it is time to control the hardware.
            </p>
          </div>

          <div className='mt-8 flex flex-col gap-3 sm:flex-row'>
            <a href='/app' className='landing-primary-cta'>
              Launch live app
              <FontAwesomeIcon icon={faArrowRightLong} />
            </a>
            <a
              href='https://discord.gaggimate.eu/'
              className='landing-secondary-cta landing-secondary-cta-dark'
            >
              Join the community
            </a>
          </div>
        </div>
      </section>
    </div>
  );
}
