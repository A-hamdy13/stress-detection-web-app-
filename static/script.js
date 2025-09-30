// Wait for DOM to be fully loaded
document.addEventListener('DOMContentLoaded', function() {
    const stressCtx = document.getElementById('stressChart').getContext('2d');
    const tempCtx = document.getElementById('tempChart').getContext('2d');
    const hrCtx = document.getElementById('hrChart').getContext('2d');
    const emojiBox = document.getElementById('emojiBox');

    let stressChart, tempChart, hrChart;

    // Register zoom plugin if available
    if (Chart.registerPlugin) {
        try {
            Chart.registerPlugin(ChartZoom);
        } catch (e) {
            console.warn("Chart zoom plugin not registered:", e);
        }
    }

    function getEmoji(stressed) {
        return stressed ? '😟' : '😊';
    }

    function updateCharts(data) {
        if (!data || data.length === 0) {
            console.log("No data received");
            emojiBox.textContent = "No data 🔄";
            return;
        }
        
        const times = data.map(d => new Date(d.time).toLocaleTimeString());
        const stress = data.map(d => d.stressed);
        const temps = data.map(d => d.temp);
        const hrs = data.map(d => d.hr);

        // Update emoji with latest stress state
        if (data.length > 0) {
            const latestStress = stress[stress.length - 1];
            emojiBox.textContent = getEmoji(latestStress);
            console.log("Latest stress:", latestStress);
        }

        // Create or update charts
        if (!stressChart) {
            console.log("Creating new charts");
            
            // Create Stress Chart
            stressChart = new Chart(stressCtx, {
                type: 'line',
                data: {
                    labels: times,
                    datasets: [{
                        label: 'Stress Status',
                        data: stress,
                        borderColor: 'red',
                        backgroundColor: 'rgba(255, 0, 0, 0.1)',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.1
                    }]
                },
                options: getChartOptions('Stress (0=No, 1=Yes)')
            });
            
            // Create Temperature Chart
            tempChart = new Chart(tempCtx, {
                type: 'line',
                data: {
                    labels: times,
                    datasets: [{
                        label: 'Temperature (°C)',
                        data: temps,
                        borderColor: 'orange',
                        backgroundColor: 'rgba(255, 165, 0, 0.1)',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.1
                    }]
                },
                options: getChartOptions('Temperature (°C)')
            });
            
            // Create Heart Rate Chart
            hrChart = new Chart(hrCtx, {
                type: 'line',
                data: {
                    labels: times,
                    datasets: [{
                        label: 'Heart Rate (BPM)',
                        data: hrs,
                        borderColor: 'blue',
                        backgroundColor: 'rgba(0, 0, 255, 0.1)',
                        borderWidth: 2,
                        fill: false,
                        tension: 0.1
                    }]
                },
                options: getChartOptions('Heart Rate (BPM)')
            });
        } else {
            console.log("Updating existing charts");
            
            // Update Stress Chart
            stressChart.data.labels = times;
            stressChart.data.datasets[0].data = stress;
            stressChart.update();
            
            // Update Temperature Chart
            tempChart.data.labels = times;
            tempChart.data.datasets[0].data = temps;
            tempChart.update();
            
            // Update Heart Rate Chart
            hrChart.data.labels = times;
            hrChart.data.datasets[0].data = hrs;
            hrChart.update();
        }
    }

    async function fetchData() {
        try {
            console.log("Fetching data...");
            const response = await fetch('/data');
            if (!response.ok) {
                throw new Error(`HTTP error! Status: ${response.status}`);
            }
            const data = await response.json();
            console.log(`Fetched ${data.length} data points`);
            updateCharts(data);
        } catch (error) {
            console.error('Fetch error:', error);
            emojiBox.textContent = "Error 🔄";
        }
    }

    function getChartOptions(title) {
        return {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: { 
                    display: true, 
                    title: { display: true, text: 'Time' } 
                },
                y: { 
                    display: true, 
                    title: { display: true, text: title } 
                }
            },
            plugins: {
                zoom: {
                    pan: { 
                        enabled: true, 
                        mode: 'x' 
                    },
                    zoom: {
                        wheel: { 
                            enabled: true 
                        },
                        pinch: { 
                            enabled: true 
                        },
                        mode: 'x'
                    }
                }
            }
        };
    }

    // Fetch data immediately and then every 30 seconds
    fetchData();
    setInterval(fetchData, 5000);
});