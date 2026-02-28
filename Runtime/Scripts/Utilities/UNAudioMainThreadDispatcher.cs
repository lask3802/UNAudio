using System;
using System.Collections.Concurrent;
using UnityEngine;

namespace UNAudio
{
    /// <summary>
    /// Marshals callbacks from background threads to the Unity main thread.
    /// Auto-creates a hidden DontDestroyOnLoad GameObject when first used.
    /// </summary>
    public class UNAudioMainThreadDispatcher : MonoBehaviour
    {
        private static UNAudioMainThreadDispatcher instance;
        private static readonly ConcurrentQueue<Action> queue = new ConcurrentQueue<Action>();

        /// <summary>
        /// Enqueue an action to be executed on the main thread during the next Update.
        /// </summary>
        public static void Enqueue(Action action)
        {
            if (action == null) return;
            EnsureInstance();
            queue.Enqueue(action);
        }

        private static void EnsureInstance()
        {
            if (instance != null) return;

            var go = new GameObject("[UNAudio] MainThreadDispatcher");
            go.hideFlags = HideFlags.HideAndDontSave;
            DontDestroyOnLoad(go);
            instance = go.AddComponent<UNAudioMainThreadDispatcher>();
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void InitializeOnLoad()
        {
            EnsureInstance();
        }

        private void Update()
        {
            while (queue.TryDequeue(out Action action))
            {
                try
                {
                    action.Invoke();
                }
                catch (Exception ex)
                {
                    Debug.LogException(ex);
                }
            }
        }

        private void OnDestroy()
        {
            if (instance == this)
                instance = null;
        }
    }
}
