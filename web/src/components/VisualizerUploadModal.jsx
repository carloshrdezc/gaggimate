import { useEffect, useRef, useState } from 'preact/hooks';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faEye } from '@fortawesome/free-solid-svg-icons/faEye';
import { faEyeSlash } from '@fortawesome/free-solid-svg-icons/faEyeSlash';
import { faSpinner } from '@fortawesome/free-solid-svg-icons/faSpinner';
import Dialog from './Dialog';

export default function VisualizerUploadModal({
  isOpen,
  onClose,
  onUpload,
  isUploading = false,
  shotInfo,
}) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [showPassword, setShowPassword] = useState(false);
  const [rememberCredentials, setRememberCredentials] = useState(false);
  const usernameRef = useRef(null);

  const handleSubmit = async e => {
    e.preventDefault();
    if (!username.trim() || !password.trim()) {
      alert('Please enter both username and password');
      return;
    }

    try {
      await onUpload(username.trim(), password, rememberCredentials);

      // Remember only the username when requested; passwords are never stored.
      if (rememberCredentials) {
        localStorage.setItem('visualizer_username', username.trim());
        localStorage.setItem('visualizer_remember', 'true');
      } else {
        localStorage.removeItem('visualizer_username');
        localStorage.removeItem('visualizer_remember');
      }
      localStorage.removeItem('visualizer_password');

      // Clear form and close modal on success
      setUsername('');
      setPassword('');
      onClose();
    } catch (error) {
      // Error handling is done in parent component
      console.error('Upload failed:', error);
    }
  };

  const handleClose = () => {
    if (!isUploading) {
      setUsername('');
      setPassword('');
      onClose();
    }
  };

  // Load a saved username and remove any legacy password when the modal opens.
  useEffect(() => {
    if (isOpen) {
      const savedUsername = localStorage.getItem('visualizer_username');
      const savedRemember = localStorage.getItem('visualizer_remember') === 'true';

      localStorage.removeItem('visualizer_password');
      setPassword('');

      if (savedRemember && savedUsername) {
        setUsername(savedUsername);
        setRememberCredentials(true);
      } else {
        setUsername('');
        setRememberCredentials(false);
      }
    }
  }, [isOpen]);

  if (!isOpen) return null;

  return (
    <Dialog
      open={isOpen}
      onClose={handleClose}
      title='Upload to Visualizer.coffee'
      description='Enter your Visualizer.coffee credentials to upload the current shot.'
      initialFocusRef={usernameRef}
      closeOnBackdropClick={false}
      panelClassName='max-h-[90vh] w-full max-w-md overflow-y-auto rounded-lg bg-white shadow-xl dark:bg-gray-800'
      backdropClassName='bg-opacity-50 bg-black'
    >
      <div className='p-6'>
        {/* Dialog supplies the accessible name via its sr-only <h2 id={...}> + aria-labelledby
            (PRO-180 review P3). We render the same text visibly here, but as a non-heading
            element so screen-reader users don't hear the title twice and the panel doesn't
            carry two headings for the same name. */}
        <div className='mb-4 flex items-center justify-between'>
          <span aria-hidden='true' className='text-lg font-semibold'>
            Upload to Visualizer.coffee
          </span>
          {!isUploading && (
            <button
              type='button'
              onClick={handleClose}
              aria-label='Close upload dialog'
              className='text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200'
            >
              ✕
            </button>
          )}
        </div>

        {shotInfo && (
          <div className='mb-4 rounded-md bg-gray-100 p-3 dark:bg-gray-700'>
            <p className='text-sm text-gray-600 dark:text-gray-300'>
              <strong>Shot:</strong> {shotInfo.profile}
            </p>
            <p className='text-sm text-gray-600 dark:text-gray-300'>
              <strong>Date:</strong> {new Date(shotInfo.timestamp * 1000).toLocaleString()}
            </p>
            <p className='text-sm text-gray-600 dark:text-gray-300'>
              <strong>Duration:</strong> {(shotInfo.duration / 1000).toFixed(1)}s
            </p>
            {shotInfo.volume > 0 && (
              <p className='text-sm text-gray-600 dark:text-gray-300'>
                <strong>Yield:</strong> {shotInfo.volume}g
              </p>
            )}
          </div>
        )}

        <form onSubmit={handleSubmit} className='space-y-4' name='visualizer-login' method='post'>
          <div>
            <label htmlFor='username' className='mb-1 block text-sm font-medium'>
              Visualizer.coffee Username
            </label>
            <input
              ref={usernameRef}
              id='username'
              name='username'
              type='text'
              value={username}
              onChange={e => setUsername(e.target.value)}
              disabled={isUploading}
              className='w-full rounded-md border border-gray-300 px-3 py-2 focus:ring-2 focus:ring-blue-500 focus:outline-none disabled:opacity-50 dark:border-gray-600 dark:bg-gray-700 dark:text-white'
              placeholder='Enter your username'
              autoComplete='username'
              required
            />
          </div>

          <div>
            <label htmlFor='password' className='mb-1 block text-sm font-medium'>
              Password
            </label>
            <div className='relative'>
              <input
                id='password'
                name='password'
                type={showPassword ? 'text' : 'password'}
                value={password}
                onChange={e => setPassword(e.target.value)}
                disabled={isUploading}
                className='w-full rounded-md border border-gray-300 px-3 py-2 pr-10 focus:ring-2 focus:ring-blue-500 focus:outline-none disabled:opacity-50 dark:border-gray-600 dark:bg-gray-700 dark:text-white'
                placeholder='Enter your password'
                autoComplete='current-password'
                required
              />
              <button
                type='button'
                onClick={() => setShowPassword(!showPassword)}
                disabled={isUploading}
                className='absolute inset-y-0 right-0 flex items-center pr-3 text-gray-400 hover:text-gray-600 disabled:opacity-50'
              >
                <FontAwesomeIcon icon={showPassword ? faEyeSlash : faEye} />
              </button>
            </div>
          </div>

          <div className='flex items-center'>
            <input
              id='remember'
              type='checkbox'
              checked={rememberCredentials}
              onChange={e => setRememberCredentials(e.target.checked)}
              disabled={isUploading}
              className='h-4 w-4 rounded border-gray-300 text-blue-600 focus:ring-blue-500 disabled:opacity-50'
            />
            <label htmlFor='remember' className='ml-2 text-sm text-gray-600 dark:text-gray-300'>
              Remember username on this browser
            </label>
          </div>

          <div className='flex justify-end space-x-3 pt-4'>
            <button
              type='button'
              onClick={handleClose}
              disabled={isUploading}
              className='rounded-md bg-gray-200 px-4 py-2 text-sm font-medium text-gray-700 hover:bg-gray-300 disabled:opacity-50 dark:bg-gray-600 dark:text-gray-200 dark:hover:bg-gray-500'
            >
              Cancel
            </button>
            <button
              type='submit'
              disabled={isUploading || !username.trim() || !password.trim()}
              className='flex items-center space-x-2 rounded-md bg-blue-600 px-4 py-2 text-sm font-medium text-white hover:bg-blue-700 disabled:opacity-50'
            >
              {isUploading && <FontAwesomeIcon icon={faSpinner} spin />}
              <span>{isUploading ? 'Uploading...' : 'Upload Shot'}</span>
            </button>
          </div>
        </form>

        <div className='mt-4 text-xs text-gray-500 dark:text-gray-400'>
          <p>
            Only your username is stored locally on this browser when you choose to remember it.
            Your password is used only for the current upload.
          </p>
        </div>
      </div>
    </Dialog>
  );
}
